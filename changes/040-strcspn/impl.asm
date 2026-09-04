; changes/040-strcspn/impl.asm
; size_t wia_strcspn(const char* str, const char* set)   [Win64: rcx, rdx -> rax]
;
; Length of the initial run of str made up entirely of bytes NOT in set (i.e. the
; index of the first byte that IS in set, or strlen(str) if none). Byte counterpart of
; 037-wcscspn / index form of strpbrk. ucrtbase does the naive O(n*m) scan (~1.9 GB/s).
;
; Scan str 32 bytes at a time; per block OR vpcmpeqb against every set byte (broadcast
; straight from memory) and the ==0 terminator mask, and stop at the first such byte.
; Return = that byte index. The terminator is a stop, so a str with no set member
; returns its length and the scan never runs past the string. Empty set -> strlen.
;
; Page-safe: 32-aligned base + prologue mask-shift, then a 32-aligned loop. Sets >= 32
; bytes take a correct scalar fallback. No non-volatile regs, no stack.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strcspn PROC
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
        vpxor     ymm1, ymm1, ymm1                  ; ymm1 = 0
        mov       r11, r8
        and       r11, -32
        mov       ecx, r8d
        and       ecx, 31

        vmovdqu   ymm6, ymmword ptr [r11]
        vpcmpeqb  ymm3, ymm6, ymm1                  ; == 0 (terminator)
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
        test      r10, r10
        jz        have0
in_lp0:
        vpbroadcastb ymm0, byte ptr [r9 + rax]
        vpcmpeqb  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp0
have0:
        vpor      ymm5, ymm4, ymm3                  ; stop = in-set OR terminator
        vpmovmskb edx, ymm5
        shr       edx, cl
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx
        mov       eax, edx
        vzeroupper
        ret

nextblk:
        add       r11, 32
        vmovdqu   ymm6, ymmword ptr [r11]
        vpcmpeqb  ymm3, ymm6, ymm1
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
        test      r10, r10
        jz        have1
in_lp1:
        vpbroadcastb ymm0, byte ptr [r9 + rax]
        vpcmpeqb  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp1
have1:
        vpor      ymm5, ymm4, ymm3
        vpmovmskb edx, ymm5
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx
        lea       rax, [r11 + rdx]
        sub       rax, r8
        vzeroupper
        ret

scalar_setup:
        xor       rax, rax
sc_next:
        movzx     r10d, byte ptr [r8 + rax]
        test      r10b, r10b
        jz        ret_have                           ; terminator -> stop
        xor       rcx, rcx
sc_in:
        movzx     edx, byte ptr [r9 + rcx]
        test      dl, dl
        jz        sc_adv                             ; end of set, not a member
        cmp       dl, r10b
        je        ret_have                           ; in set -> stop
        inc       rcx
        jmp       sc_in
sc_adv:
        inc       rax
        jmp       sc_next

ret_have:
        vzeroupper
        ret
wia_strcspn ENDP
END
