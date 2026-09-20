; changes/270-convertsidtostringsid/impl.asm
;   BOOL wia_sid2str(const void* sid, wchar_t** out)      [Win64: rcx, rdx -> eax]
;
; advapi32!ConvertSidToStringSidW. discovery/sid_inet_bstr.c measured the shipped export at
; 181.45 ns against 73.73 ns for ntdll!RtlConvertSidToUnicodeString on the same SID, with the
; LocalAlloc/LocalFree pair the contract requires costing 40.41 ns of that -- so about 67 ns of the
; 181 was neither the formatting nor the allocation.
;
; --------------------------------------------------------------------------------------------------
; This file is an envelope, not a second formatter. probes/contract.c formats every shape of SID
; with advapi32's export AND with ntdll's and compares the two strings -- the ordinary counts, every
; revision, every sub-authority count 0..255, and the identifier authority at every decimal and
; hexadecimal boundary. They agree on all of it, INCLUDING on what they refuse. So the text comes
; from change 067, which this links against, the same way change 268 links 016 and 034 rather than
; copying either: pasting a formatter here would create a second copy that a future correction would
; silently leave behind, which is how change 132's extension rule ended up wrong in four landed
; changes at once.
;
; "They agree" was not assumed. Change 268 was built on exactly this bet about two functions
; documented as a pair, and found four behaviours where they differ. Here they do not, and the
; probe that says so is in the repository.
;
; --------------------------------------------------------------------------------------------------
; What the envelope owns, and every line of it is a measurement:
;
;   1. The failure codes are Win32 and there are only two. a NULL SID or a NULL out-pointer is
;      ERROR_INVALID_PARAMETER; everything the formatter refuses -- a revision other than 1, a
;      sub-authority count above 15 -- is ERROR_INVALID_SID. Nothing maps to anything else.
;
;   2. On failure the output pointer is left alone, measured with a poison value. Its sibling
;      ConvertStringSidToSidW (change 269) CLEARS it for three characters out of 65535; this one
;      never does, and the two were measured separately rather than assumed to match.
;
;   3. On success the last error becomes zero, from any starting value (probes/validate.c: five
;      values at four lengths, twenty out of twenty).
;
;   4. The block is LocalAlloc(LMEM_FIXED) of exactly (characters + 1) * 2 Bytes, because the caller
;      frees it with LocalFree. That is called, not imitated -- see alloc.c.
;
;   5. a SID that is not fully readable is a refusal when its sub-authority array runs off the end
;      and a FAULT when only its six identifier-authority bytes do (probes/truncated.c). That rule
;      lives in change 067 and is inherited here for free, because it is the same formatter.
;
; ISA: baseline x64 here; change 067 brings AVX2 for its copy.

OPTION PROC:PRIVATE
PUBLIC wia_sid2str

EXTERN wia_sidfmt:PROC                      ; change 067
EXTERN wia_sid2str_alloc:PROC
EXTERN wia_sid2str_ok:PROC
EXTERN wia_sid2str_err_param:PROC
EXTERN wia_sid2str_err_invalid:PROC
EXTERN wia_sid2str_err_nomem:PROC

; The frame. Nothing is pushed in the body and everything called from it is external with shadow
; space of its own, which is what makes the unwind data expressible (change 268's note).
;
;   [rsp+0..31]     shadow space for the calls out
;   [rsp+32..47]    a UNICODE_STRING: Length, MaximumLength, then Buffer at +8
;   [rsp+48..847]   the temporary string -- 400 characters, against a longest result of 184
;   [rsp+848]       the caller's `out`
USTR      EQU 32
UBUF      EQU 40
TMP       EQU 48
POUT      EQU 848
FRAME_SZ  EQU 856                           ; 856 mod 16 = 8, so rsp is aligned at every call site

.code

ALIGN 16
wia_sid2str PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        sub       rsp, FRAME_SZ
        .allocstack FRAME_SZ
        .endprolog

        test      rcx, rcx
        jz        bad_param
        test      rdx, rdx
        jz        bad_param
        mov       rsi, rcx                          ; the SID
        mov       qword ptr [rsp + POUT], rdx

        ; format into the temporary. The room is 800 bytes against a longest possible result of 368,
        ; so this call cannot return STATUS_BUFFER_OVERFLOW and every non-zero status is a refusal.
        lea       rbx, [rsp + USTR]
        mov       word ptr [rbx], 0
        mov       word ptr [rbx + 2], 800
        lea       rax, [rsp + TMP]
        mov       qword ptr [rbx + 8], rax
        mov       rcx, rbx
        mov       rdx, rsi
        xor       r8d, r8d                          ; Allocate = FALSE
        call      wia_sidfmt
        test      eax, eax
        jnz       bad_sid                           ; the only other outcome is a refusal

        movzx     r12d, word ptr [rbx]              ; Length, in bytes, without the terminator
        lea       ecx, [r12d + 2]                   ; ... and the block holds the terminator too
        call      wia_sid2str_alloc
        test      rax, rax
        jz        no_mem
        mov       rdi, rax
        lea       rsi, [rsp + TMP]
        lea       r8d, [r12d + 2]                   ; bytes to move, the terminator included

        ; exactly r8d BYTES. The block is (characters+1)*2 and not one byte more, so the tail is an
        ; OVERLAPPING chunk from the end rather than a rounded-up one -- the rule change 016 had to
        ; be corrected for. The shortest result is "S-1-0" and a NUL, twelve bytes, so the
        ; eight-byte path always has something to work with.
        cmp       r8d, 32
        jb        cp_small
        xor       ecx, ecx
cp32:   vmovdqu   ymm0, ymmword ptr [rsi + rcx]
        vmovdqu   ymmword ptr [rdi + rcx], ymm0
        add       ecx, 32
        lea       edx, [ecx + 32]
        cmp       edx, r8d
        jbe       cp32
        mov       edx, r8d
        sub       edx, 32
        vmovdqu   ymm0, ymmword ptr [rsi + rdx]
        vmovdqu   ymmword ptr [rdi + rdx], ymm0
        vzeroupper
        jmp       cp_done
cp_small:
        xor       ecx, ecx
cp8:    mov       rax, qword ptr [rsi + rcx]
        mov       qword ptr [rdi + rcx], rax
        add       ecx, 8
        lea       edx, [ecx + 8]
        cmp       edx, r8d
        jbe       cp8
        mov       edx, r8d
        sub       edx, 8
        mov       rax, qword ptr [rsi + rdx]
        mov       qword ptr [rdi + rdx], rax
cp_done:
        mov       rcx, qword ptr [rsp + POUT]
        mov       qword ptr [rcx], rdi
        call      wia_sid2str_ok                    ; the last error becomes zero on success
        mov       eax, 1
        jmp       epi

bad_sid:
        call      wia_sid2str_err_invalid
        xor       eax, eax
        jmp       epi
bad_param:
        call      wia_sid2str_err_param
        xor       eax, eax
        jmp       epi
no_mem:
        call      wia_sid2str_err_nomem
        xor       eax, eax
epi:
        add       rsp, FRAME_SZ
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_sid2str ENDP

END
