; changes/133-strstrw/impl.asm
; PWSTR wia_strstrw(PCWSTR pszFirst, PCWSTR pszSrch)   [Win64: rcx, rdx -> rax]
;
; Reimplements shlwapi!StrStrW (ordinal, case-sensitive substring search). shlwapi's is a scalar scan
; (~2.2 cycles/char -- 124 ns for a 254-char haystack, 466 ns for 1024).
;
; Contract: identical to C's wcsstr EXCEPT that an **empty needle returns NULL**, where wcsstr returns
; the haystack. Verified against the live export (StrStrW(L"abcdef", L"") -> NULL).
;
; Method: AVX2 scan for the needle's FIRST character -- each 32-byte block compared against both that
; character and 0, first stop wins (the 003/131 scheme) -- then a scalar verify of the remainder. This
; deliberately avoids the two-char-anchor trick: the anchor would need to load needle-length ahead of
; the current block, which can cross into an unmapped page past the terminator. Here the vector scan
; never passes the terminator, and the verify stops at the first mismatch -- and the terminator
; mismatches any non-NUL needle character -- so no read ever goes past the string.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strstrw PROC
        push      rsi
        push      rdi
        mov       rsi, rdx                          ; needle
        mov       rdi, rcx                          ; haystack cursor
        movzx     eax, word ptr [rsi]
        test      ax, ax
        jz        ss_null                           ; empty needle -> NULL (diverges from wcsstr)
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2                     ; needle[0]
        vpxor     ymm3, ymm3, ymm3

ss_outer:
        mov       r11, rdi
        mov       r9, rdi
        and       r9, -32                           ; aligned-down load address
        mov       ecx, r11d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb edx, ymm4                         ; == needle[0]
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4                         ; == 0
        or        r8d, edx                          ; stop = match or terminator
        shr       r8d, cl                           ; drop bytes before the cursor
        shr       edx, cl
        test      r8d, r8d
        jz        ss_loop
        tzcnt     r8d, r8d
        bt        edx, r8d
        jnc       ss_null                           ; terminator came first
        lea       r10, [r11 + r8]
        jmp       ss_verify
ss_loop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb edx, ymm4
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4
        or        r8d, edx
        test      r8d, r8d
        jz        ss_loop
        tzcnt     r8d, r8d
        bt        edx, r8d
        jnc       ss_null
        lea       r10, [r9 + r8]

ss_verify:
        mov       rax, r10
        mov       rcx, rsi
ss_vloop:
        add       rax, 2
        add       rcx, 2
        movzx     edx, word ptr [rcx]
        test      dx, dx
        jz        ss_found                          ; needle exhausted -> match
        movzx     r8d, word ptr [rax]
        cmp       dx, r8w
        je        ss_vloop
        lea       rdi, [r10 + 2]                    ; mismatch: resume one wchar on
        jmp       ss_outer
ss_found:
        mov       rax, r10
        vzeroupper
        pop       rdi
        pop       rsi
        ret
ss_null:
        xor       eax, eax
        vzeroupper
        pop       rdi
        pop       rsi
        ret
wia_strstrw ENDP
END
