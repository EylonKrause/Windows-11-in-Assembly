; ucrtbase.dll!strspn  --  hand-written x86-64 reimplementation (4.82x vs shipped)
; source of truth: changes/039-strspn/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/039-strspn/impl.asm
; size_t wia_strspn(const char* str, const char* set)   [Win64: rcx, rdx -> rax]
;
; Length of the initial run of str made up entirely of bytes that ARE in set. Byte
; counterpart of 036-wcsspn. ucrtbase does the naive O(n*m) scan (~1.5 GB/s).
;
; Scan str 32 bytes at a time; per block OR vpcmpeqb against every set byte (broadcast
; straight from memory -> no per-call setup, no stack frame) to get an "in-set" mask,
; then stop at the first byte NOT in set. The terminator (0) is never in set, so it is
; automatically a stop and the scan can't run past the string. Return = byte index of
; that first non-member = the span length.
;
; Page-safe: 32-aligned base + prologue mask-shift, then a 32-aligned loop. Sets >= 32
; bytes take a correct scalar fallback. Empty set -> 0. No non-volatile regs, no stack.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strspn PROC
        mov       r8, rcx                          ; str
        mov       r9, rdx                          ; set
        xor       r10, r10                          ; slen
sl_lp:
        cmp       byte ptr [r9 + r10], 0
        je        sl_done
        inc       r10
        cmp       r10, 32
        jae       scalar_setup
        jmp       sl_lp
sl_done:
        test      r10, r10
        jz        ret_zero                          ; empty set -> span 0

        mov       r11, r8
        and       r11, -32
        mov       ecx, r8d
        and       ecx, 31

        vmovdqu   ymm6, ymmword ptr [r11]
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
in_lp0:
        vpbroadcastb ymm0, byte ptr [r9 + rax]
        vpcmpeqb  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp0
        vpmovmskb edx, ymm4
        not       edx                               ; stop = NOT in set (terminator included)
        shr       edx, cl
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx
        mov       eax, edx                           ; byte index from str = span length
        vzeroupper
        ret

nextblk:
        add       r11, 32
        vmovdqu   ymm6, ymmword ptr [r11]
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
in_lp1:
        vpbroadcastb ymm0, byte ptr [r9 + rax]
        vpcmpeqb  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp1
        vpmovmskb edx, ymm4
        not       edx
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx
        lea       rax, [r11 + rdx]
        sub       rax, r8                            ; byte index from str
        vzeroupper
        ret

scalar_setup:
        xor       rax, rax
sc_next:
        movzx     r10d, byte ptr [r8 + rax]
        test      r10b, r10b
        jz        ret_have
        xor       rcx, rcx
sc_in:
        movzx     edx, byte ptr [r9 + rcx]
        test      dl, dl
        jz        ret_have                           ; not in set -> span ends
        cmp       dl, r10b
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
wia_strspn ENDP
END
