; advapi32.dll!ConvertStringSidToSidA  --  hand-written x86-64 reimplementation (5.71x vs shipped)
; source of truth: changes/272-convertstringsidtosida/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/272-convertstringsidtosida/impl.asm
;   BOOL wia_str2sida(const char* s, PSID* out)      [Win64: rcx, rdx -> eax]
;
; advapi32!ConvertStringSidToSidA. discovery/sid_inet_bstr.c measured it at 343.95 ns against
; 269.14 ns for the wide form, and probes/asciilen.c put the gap at 70.90 ns on the same SID, of
; which MultiByteToWideChar itself is only 24.70 ns.
;
; --------------------------------------------------------------------------------------------------
; The ANSI form is a widening and then the wide parser, and that was measured.
;
; probes/codepage.c asks the ANSI export and the wide export the same question, the wide one being
; given MultiByteToWideChar(CP_ACP, 0, s, -1, ...) of the same bytes, and compares the BOOL,
; GetLastError, the fate of the output pointer and every byte of the SID:
;
;     every byte 0x01..0xFF, leading and trailing, in all three fields    1530 cases, 0 differ
;     every printable ASCII pair against the alias table                  9025 cases, 0 differ
;     the byte sequences that may not translate at all                       7 cases, 0 differ
;     the SDDL terminators, which make a FAILING call clear the pointer            0 differ
;
; So the parser is change 269, linked in, and this file is the widening.
;
; --------------------------------------------------------------------------------------------------
; The widening is a zero extension whenever the input is ASCII, and that is proved rather than
; ASSUMED.
;
; probes/asciilen.c asked whether every byte below 0x80 is the same code point under every code page
; Windows can use as the system ANSI setting, all 22 of them, including the four DBCS pages and
; 65001, over every byte 0x00..0x7F. ZERO counterexamples. So when no byte of the input has its top
; bit set, VPMOVZXBW is the same answer as the code page would give, and no code page is consulted.
;
; A high byte anywhere falls back to MultiByteToWideChar. That is not a formality: change 269 found
; the wide parser accepting 180 different code units as decimal digits and 25 as whitespace, so what
; a byte above 0x7F means is genuinely a code-page question.
;
; The scan finds the length and answers that question in one pass. Vpmovmskb extracts the top bit of
; every byte, which IS the "at or above 0x80" test, so the same 32-byte load yields the terminator
; mask (through a compare with zero) and the non-ASCII mask. The first block is loaded ALIGNED DOWN
; and the bits before the string are shifted out, a 32-byte aligned load never crosses a page
; boundary, so it cannot touch a page the string does not already occupy. That is change 225's rule,
; and the reason for it is that reading past a string that ends near a page boundary faults.
;
; --------------------------------------------------------------------------------------------------
; The temporary cannot be a fixed buffer. probes/asciilen.c handed the shipped export a megabyte of
; junk and got ERROR_INVALID_SID rather than a crash, and a legal 254-sub-authority SID string is
; already about 2800 characters. Up to 1023 characters the temporary is the frame; past that it is
; allocated and freed, and the free preserves the last error because the parser has already set it.
;
; Isa: AVX2 + BMI2 (bzhi) for the scan; vpmovzxbw for the widening.

OPTION PROC:PRIVATE
PUBLIC wia_str2sida

EXTERN wia_str2sid:PROC                     ; change 269
EXTERN wia_sid_err_param:PROC               ; change 269's aliases.c
EXTERN wia_sida_widen:PROC
EXTERN wia_sida_alloc:PROC
EXTERN wia_sida_free_keep:PROC
EXTERN wia_sida_err_nomem:PROC

;   [rsp+0..31]      shadow space for the calls out
;   [rsp+32..2079]   the widened string, 1024 characters
;   [rsp+2080]       the allocated temporary, or zero
;   [rsp+2088]       the parser's answer, across the free
TMPW      EQU 32
TMPCH     EQU 1024                          ; characters the frame holds, terminator included
PALLOC    EQU 2080
PANS      EQU 2088
FRAME_SZ  EQU 2104                          ; 2104 mod 16 = 8, so rsp is aligned at every call site

.code

ALIGN 16
wia_str2sida PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      rbp
        .pushreg  rbp
        push      r12
        .pushreg  r12
        push      r13
        .pushreg  r13
        sub       rsp, FRAME_SZ
        .allocstack FRAME_SZ
        .endprolog

        test      rcx, rcx
        jz        bad_param
        test      rdx, rdx
        jz        bad_param
        mov       rsi, rcx                        ; the bytes
        mov       rbx, rdx                        ; the caller's out
        mov       qword ptr [rsp + PALLOC], 0

        ; ---------------- one pass: the length, and whether any byte has its top bit set ----------
        mov       r8, rsi
        mov       rdx, rsi
        and       rdx, -32                        ; aligned down: the same page as rsi, always
        mov       ecx, esi
        and       ecx, 31                         ; bytes of this block that precede the string
        vpxor     ymm1, ymm1, ymm1
        xor       r9d, r9d                        ; the non-ASCII accumulator
        vmovdqa   ymm0, ymmword ptr [rdx]
        vpmovmskb r10d, ymm0                      ; the top bit of every byte
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        shr       eax, cl                         ; after this, bit i is byte rsi[i]
        shr       r10d, cl
        test      eax, eax
        jnz       sc_first
        or        r9d, r10d
        add       rdx, 32
sc_loop:
        vmovdqa   ymm0, ymmword ptr [rdx]
        vpmovmskb r10d, ymm0
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       sc_hit
        or        r9d, r10d
        add       rdx, 32
        jmp       sc_loop
sc_hit:
        tzcnt     ecx, eax
        bzhi      r10d, r10d, ecx                 ; only the bytes BEFORE the terminator count
        or        r9d, r10d
        add       rdx, rcx
        sub       rdx, r8
        mov       r12, rdx                        ; the length, in bytes
        jmp       sc_done
sc_first:
        tzcnt     ecx, eax
        bzhi      r10d, r10d, ecx
        or        r9d, r10d
        mov       r12d, ecx
sc_done:
        vzeroupper

        ; ---------------- the temporary: the frame, or an allocation ----------------
        lea       rdi, [rsp + TMPW]
        cmp       r12, TMPCH - 1
        jb        have_tmp
        lea       rcx, [r12 + 1]
        shl       rcx, 1                          ; (length + 1) characters, two bytes each, in 64
        call      wia_sida_alloc                  ;   bits so an absurd length fails rather than wraps
        test      rax, rax
        jz        no_mem
        mov       qword ptr [rsp + PALLOC], rax
        mov       rdi, rax
have_tmp:

        test      r9d, r9d
        jnz       slow_widen

        ; ---------------- pure ASCII: a zero extension, no code page ----------------
        ; Sixteen bytes an iteration while a whole sixteen remain BEFORE the terminator. The tail and
        ; the terminator go one at a time: reading sixteen bytes that run past the NUL could cross
        ; into a page the string does not occupy, which is the fault change 225 documents.
        xor       ecx, ecx
        mov       rdx, r12
        cmp       rdx, 16
        jb        ze_tail
ze16:   vpmovzxbw ymm0, xmmword ptr [rsi + rcx]
        vmovdqu   ymmword ptr [rdi + rcx*2], ymm0
        add       rcx, 16
        lea       rax, [rcx + 16]
        cmp       rax, rdx
        jbe       ze16
        vzeroupper
ze_tail:
        cmp       rcx, r12
        jae       ze_nul
        movzx     eax, byte ptr [rsi + rcx]
        mov       word ptr [rdi + rcx*2], ax
        inc       rcx
        jmp       ze_tail
ze_nul:
        mov       word ptr [rdi + r12*2], 0
        jmp       parse

        ; ---------------- anything else: the real conversion ----------------
slow_widen:
        mov       rcx, rsi
        lea       edx, [r12d + 1]                 ; the terminator is converted too
        mov       r8, rdi
        lea       r9d, [r12d + 1]
        call      wia_sida_widen
        test      eax, eax
        jz        bad_param                       ; it cannot fail with these arguments; refuse if it does

parse:
        mov       rcx, rdi
        mov       rdx, rbx
        call      wia_str2sid
        mov       dword ptr [rsp + PANS], eax
        mov       rcx, qword ptr [rsp + PALLOC]
        test      rcx, rcx
        jz        no_free
        call      wia_sida_free_keep              ; the parser has already set the last error
no_free:
        mov       eax, dword ptr [rsp + PANS]
        jmp       epi

bad_param:
        call      wia_sid_err_param
        xor       eax, eax
        jmp       epi
no_mem:
        call      wia_sida_err_nomem
        xor       eax, eax
epi:
        add       rsp, FRAME_SZ
        pop       r13
        pop       r12
        pop       rbp
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_str2sida ENDP

END
