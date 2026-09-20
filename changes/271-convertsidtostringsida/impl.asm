; changes/271-convertsidtostringsida/impl.asm
;   BOOL wia_sid2stra(const void* sid, char** out)      [Win64: rcx, rdx -> eax]
;
; advapi32!ConvertSidToStringSidA. discovery/sid_inet_bstr.c measured it at 254.59 ns against
; 181.45 ns for the wide form -- a 73 ns gap, which is about what a code-page conversion of a
; 44-character string costs.
;
; --------------------------------------------------------------------------------------------------
; The ANSI form is the wide form narrowed one byte per character, and that was measured, not assumed.
;
; probes/contract.c asks both exports the same question over every shape of SID -- every
; sub-authority count 0..255, every revision 0..255, the identifier authority at every decimal and
; hexadecimal boundary at six counts, the NULL arguments, and the last-error rule -- and compares the
; characters, the refusals, GetLastError, LocalSize, LocalFlags and the fate of the output pointer.
; ZERO DIFFERENCES. It then repeats the comparison under four thread locales including Shift-JIS and
; UTF-8, because a SID string being ASCII "by construction" is exactly the kind of reasoning changes
; 021 and 027 were built on before their code pages were measured. Still zero.
;
; So there is no code page in this file. A SID string is 'S', '-', '0'..'9', 'A'..'F' and 'x', all
; below 0x80, so the narrowing is a saturating byte pack that cannot clip -- sixteen characters per
; iteration through VPACKUSWB, which needs no lane fix-up at 128 bits.
;
; --------------------------------------------------------------------------------------------------
; Everything else is change 270's contract, and it is linked rather than restated: the formatter is
; change 067, the failure codes and the allocation are change 270's alloc.c, and the block is
; LocalAlloc(LMEM_FIXED) of exactly characters+1 bytes -- one byte per character here rather than
; two, which probes/contract.c measured at four counts.
;
; ISA: SSE2/AVX for the pack (VEX-encoded, so no alignment requirement); change 067 brings AVX2.

OPTION PROC:PRIVATE
PUBLIC wia_sid2stra

EXTERN wia_sidfmt:PROC                      ; change 067
EXTERN wia_sid2str_alloc:PROC               ; change 270's alloc.c -- the same four helpers
EXTERN wia_sid2str_ok:PROC
EXTERN wia_sid2str_err_param:PROC
EXTERN wia_sid2str_err_invalid:PROC
EXTERN wia_sid2str_err_nomem:PROC

;   [rsp+0..31]     shadow space for the calls out
;   [rsp+32..47]    a UNICODE_STRING: Length, MaximumLength, then Buffer at +8
;   [rsp+48..847]   the temporary wide string -- 400 characters against a longest result of 184
;   [rsp+848]       the caller's `out`
USTR      EQU 32
TMP       EQU 48
POUT      EQU 848
FRAME_SZ  EQU 856                           ; 856 mod 16 = 8, so rsp is aligned at every call site

.code

ALIGN 16
wia_sid2stra PROC FRAME
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
        mov       rsi, rcx
        mov       qword ptr [rsp + POUT], rdx

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
        jnz       bad_sid                           ; 800 bytes of room, so this is a refusal

        movzx     r12d, word ptr [rbx]
        shr       r12d, 1                           ; characters
        lea       ecx, [r12d + 1]                   ; one byte each, and the terminator
        call      wia_sid2str_alloc
        test      rax, rax
        jz        no_mem
        mov       rdi, rax
        lea       rsi, [rsp + TMP]

        ; NARROW. VPACKUSWB saturates, and every character here is below 0x80, so nothing clips --
        ; which probes/contract.c established over every shape of SID rather than by reasoning about
        ; what a SID string "must" contain. Sixteen characters an iteration; at 128 bits the pack
        ; needs no lane fix-up.
        mov       ecx, r12d
        cmp       ecx, 16
        jb        pk_small
        xor       edx, edx
pk16:   vmovdqu   xmm0, xmmword ptr [rsi + rdx*2]
        vmovdqu   xmm1, xmmword ptr [rsi + rdx*2 + 16]
        vpackuswb xmm0, xmm0, xmm1
        vmovdqu   xmmword ptr [rdi + rdx], xmm0
        add       edx, 16
        lea       eax, [edx + 16]
        cmp       eax, ecx
        jbe       pk16
        ; the last sixteen, OVERLAPPING backwards rather than rounded up: the block is exactly
        ; characters+1 bytes, so a rounded-up store would run past it. Change 016's rule.
        mov       edx, ecx
        sub       edx, 16
        vmovdqu   xmm0, xmmword ptr [rsi + rdx*2]
        vmovdqu   xmm1, xmmword ptr [rsi + rdx*2 + 16]
        vpackuswb xmm0, xmm0, xmm1
        vmovdqu   xmmword ptr [rdi + rdx], xmm0
        jmp       pk_done
pk_small:
        ; five to fifteen characters: the shortest possible result is "S-1-0"
        xor       edx, edx
pk1:    movzx     eax, word ptr [rsi + rdx*2]
        mov       byte ptr [rdi + rdx], al
        inc       edx
        cmp       edx, ecx
        jb        pk1
pk_done:
        mov       byte ptr [rdi + r12], 0

        mov       rcx, qword ptr [rsp + POUT]
        mov       qword ptr [rcx], rdi
        call      wia_sid2str_ok
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
wia_sid2stra ENDP

END
