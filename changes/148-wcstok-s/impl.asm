; changes/148-wcstok-s/impl.asm
; wchar_t* wia_wcstok_s(wchar_t* str, const wchar_t* delim, wchar_t** ctx)   [Win64: rcx, rdx, r8 -> rax]
;
; Reimplements ucrtbase!wcstok_s. ucrtbase's costs 369 ns to pull one token out of a 254-wchar string
; (~1.45 ns/char -- even slower than the narrow strtok_s, change 147) because it is two scalar set-scans -- strspn to skip leading delimiters, then strcspn
; to find the token end. Both become one AVX2 block scan each.
;
; Contract (probed against the live export -- standard C semantics):
;   - str == NULL continues from *ctx;
;   - leading delimiters are SKIPPED but left intact in the buffer -- only the delimiter that ends a
;     token is overwritten with NUL (",,a,,b,," leaves ",,a\0,b\0,");
;   - no token left -> NULL, with *ctx pointing at the terminator;
;   - an empty delimiter set makes the whole remaining string one token.
;
; Both scans exploit the same fact used in changes 135/136: the delimiter set is itself NUL-terminated,
; so it can never contain NUL. That means the terminator automatically ends the skip phase (it matches
; no delimiter), while the token phase must test for it explicitly.
;
; Page-safe: loads are 32-byte aligned with the leading bytes shifted out of the mask, so no load
; crosses into a page the string does not already occupy.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_wcstok_s PROC
        push      rbx
        push      rsi
        push      rdi
        mov       rsi, rdx                          ; delim
        mov       rdi, r8                           ; ctx
        test      rcx, rcx
        jnz       tk_have
        mov       rcx, [rdi]                        ; continue from *ctx
tk_have:
        vpxor     ymm3, ymm3, ymm3

        ; ================= phase 1: skip leading delimiters =================
        mov       rbx, rcx                          ; position base for this block
        mov       r9, rcx
        and       r9, -32
        mov       ecx, ebx
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1
        mov       r10, rsi
tk_s1:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        tk_d1
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       tk_s1
tk_d1:
        vpmovmskb eax, ymm1
        not       eax                               ; first byte NOT in the set (NUL qualifies)
        shr       eax, cl
        test      eax, eax
        jnz       tk_p1_here
tk_n1:
        add       r9, 32
        mov       rbx, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1
        mov       r10, rsi
tk_s1b:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        tk_d1b
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       tk_s1b
tk_d1b:
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jz        tk_n1
tk_p1_here:
        tzcnt     eax, eax
        add       rbx, rax                          ; rbx = first non-delimiter
        cmp       word ptr [rbx], 0
        jne       tk_token
        mov       [rdi], rbx                        ; nothing left
        xor       eax, eax
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret

        ; ================= phase 2: find the end of the token =================
tk_token:
        mov       r8, rbx                           ; remember the token start
        mov       r11, rbx                          ; position base
        mov       r9, rbx
        and       r9, -32
        mov       ecx, r11d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3                  ; seed with the terminator
        mov       r10, rsi
tk_s2:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        tk_d2
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       tk_s2
tk_d2:
        vpmovmskb eax, ymm1
        shr       eax, cl
        test      eax, eax
        jnz       tk_p2_here
tk_n2:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        mov       r10, rsi
tk_s2b:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        tk_d2b
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       tk_s2b
tk_d2b:
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        tk_n2
tk_p2_here:
        tzcnt     eax, eax
        add       r11, rax                          ; r11 = delimiter or terminator
        cmp       word ptr [r11], 0
        je        tk_atend
        mov       word ptr [r11], 0                 ; terminate the token
        add       r11, 2
tk_atend:
        mov       [rdi], r11
        mov       rax, r8
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_wcstok_s ENDP
END
