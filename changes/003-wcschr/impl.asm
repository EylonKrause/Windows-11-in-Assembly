; changes/003-wcschr/impl.asm
; wchar_t* wia_wcschr(const wchar_t* s, wchar_t c)   [Win64: rcx, dx -> rax]
;
; First wchar == c, else NULL; if c==0 returns the terminator (standard). Each
; block is compared against both c and 0; the first "stop" position (c or 0)
; decides. Page-safe: 32-aligned masked prologue then a 32-aligned loop, so no
; load crosses into a page the string does not already occupy.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3 (see docs/PLATFORM.md).

.code
wia_wcschr PROC
        vmovd     xmm2, edx
        vpbroadcastw ymm2, xmm2                    ; ymm2 = c broadcast
        vpxor     ymm1, ymm1, ymm1                 ; ymm1 = 0
        mov       r11, rcx                         ; r11 = s (kept)
        mov       r9, rcx
        and       r9, -32                          ; aligned-down base
        mov       ecx, r11d
        and       ecx, 31                          ; cl = byte offset of start

        vpcmpeqw  ymm0, ymm2, ymmword ptr [r9]     ; == c
        vpcmpeqw  ymm3, ymm1, ymmword ptr [r9]     ; == 0
        vpor      ymm4, ymm0, ymm3                 ; stop = c or 0
        vpmovmskb r8d, ymm4
        vpmovmskb edx, ymm0                         ; c-mask
        shr       r8d, cl
        shr       edx, cl
        test      r8d, r8d
        jz        scan
        tzcnt     r8d, r8d                          ; first stop, byte offset from s
        bt        edx, r8d                          ; was it a c-match?
        jnc       ret_null
        lea       rax, [r11 + r8]
        vzeroupper
        ret

scan:
        add       r9, 32
        vpcmpeqw  ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm1, ymmword ptr [r9]
        vpor      ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4
        test      r8d, r8d
        jz        scan
        vpmovmskb edx, ymm0
        tzcnt     r8d, r8d
        bt        edx, r8d
        jnc       ret_null
        lea       rax, [r9 + r8]
        vzeroupper
        ret

ret_null:
        xor       eax, eax
        vzeroupper
        ret
wia_wcschr ENDP
END
