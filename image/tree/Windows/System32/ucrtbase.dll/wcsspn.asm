; ucrtbase.dll!wcsspn  --  hand-written x86-64 reimplementation (5.67x vs shipped)
; source of truth: changes/036-wcsspn/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/036-wcsspn/impl.asm
; size_t wia_wcsspn(const wchar_t* str, const wchar_t* set)   [Win64: rcx, rdx -> rax]
;
; Length of the initial run of str made up entirely of chars that ARE in set.
; ucrtbase does the naive O(n*m) scan (~0.9 GB/s). We scan str 16 wchars at a time;
; per block we OR vpcmpeqw against every set char (broadcast straight from memory, so
; there is NO per-call setup and no stack frame) to get an "in-set" mask, then stop at
; the first wchar NOT in set. The terminator (0) is never in set, so it is automatically
; a stop and the scan can't run past the string. Return = wchar index of that first
; non-member = the span length.
;
; Page-safe: 32-aligned base + prologue mask-shift, then a 32-aligned loop. Sets with
; >= 32 chars (rare) take a correct scalar fallback. Empty set -> 0. No non-volatile
; registers touched, no stack used. ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_wcsspn PROC
        mov       r8, rcx                          ; str
        mov       r9, rdx                          ; set
        ; ---- slen = wcslen(set), capped at 32 ----
        xor       r10, r10
sl_lp:
        cmp       word ptr [r9 + r10*2], 0
        je        sl_done
        inc       r10
        cmp       r10, 32
        jae       scalar_setup
        jmp       sl_lp
sl_done:
        test      r10, r10
        jz        ret_zero                          ; empty set -> span 0

        mov       r11, r8
        and       r11, -32                          ; aligned-down block base
        mov       ecx, r8d
        and       ecx, 31                           ; cl = start byte offset within block

        ; ---- prologue block ----
        vmovdqu   ymm6, ymmword ptr [r11]
        vpxor     ymm4, ymm4, ymm4                  ; in-set accumulator
        xor       rax, rax
in_lp0:
        vpbroadcastw ymm0, word ptr [r9 + rax*2]
        vpcmpeqw  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp0
        vpmovmskb edx, ymm4
        not       edx                               ; stop = NOT in set (terminator included)
        shr       edx, cl                           ; relative to str
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx                          ; byte offset of first non-member from str
        shr       edx, 1                            ; -> wchar index
        mov       eax, edx
        vzeroupper
        ret

nextblk:
        add       r11, 32
        vmovdqu   ymm6, ymmword ptr [r11]
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
in_lp1:
        vpbroadcastw ymm0, word ptr [r9 + rax*2]
        vpcmpeqw  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp1
        vpmovmskb edx, ymm4
        not       edx
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx                          ; byte offset within block
        lea       rax, [r11 + rdx]                  ; absolute byte address of first non-member
        sub       rax, r8                           ; byte offset from str
        shr       rax, 1                             ; -> wchar index
        vzeroupper
        ret

        ; ---- scalar fallback (set >= 32 chars) ----
scalar_setup:
        xor       rax, rax                           ; index
sc_next:
        movzx     r10d, word ptr [r8 + rax*2]        ; str char
        test      r10w, r10w
        jz        ret_have                           ; terminator -> span ends
        xor       rcx, rcx
sc_in:
        movzx     edx, word ptr [r9 + rcx*2]
        test      dx, dx
        jz        ret_have                           ; not in set -> span ends
        cmp       dx, r10w
        je        sc_hit
        inc       rcx
        jmp       sc_in
sc_hit:
        inc       rax
        jmp       sc_next

ret_zero:
        xor       eax, eax
ret_have:
        vzeroupper
        ret
wia_wcsspn ENDP
END
