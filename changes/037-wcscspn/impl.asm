; changes/037-wcscspn/impl.asm
; size_t wia_wcscspn(const wchar_t* str, const wchar_t* set)   [Win64: rcx, rdx -> rax]
;
; Length of the initial run of str made up entirely of chars that are NOT in set
; (i.e. the index of the first char that IS in set, or strlen(str) if none). The
; index form of wcspbrk. ucrtbase does the naive O(n*m) scan (~1.3 GB/s).
;
; Scan str 16 wchars at a time; per block OR vpcmpeqw against every set char
; (broadcast straight from memory -> no per-call setup, no stack frame) to get an
; "in-set" mask, OR in the ==0 (terminator) mask, and stop at the first such wchar.
; Return = that wchar index. Because the terminator is a stop, a str with no set
; member returns its length and the scan never runs past the string.
;
; Page-safe: 32-aligned base + prologue mask-shift, then a 32-aligned loop. Sets with
; >= 32 chars take a correct scalar fallback. Empty set -> strlen (first stop is the
; terminator). No non-volatile registers, no stack. ISA: AVX2 + BMI1. Validated on Zen3.

.code
wia_wcscspn PROC
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
        ; (empty set is fine: inset accumulator stays 0, so only the terminator stops)
        vpxor     ymm1, ymm1, ymm1                  ; ymm1 = 0
        mov       r11, r8
        and       r11, -32                          ; aligned-down block base
        mov       ecx, r8d
        and       ecx, 31                           ; cl = start byte offset within block

        ; ---- prologue block ----
        vmovdqu   ymm6, ymmword ptr [r11]
        vpcmpeqw  ymm3, ymm6, ymm1                  ; == 0 (terminator)
        vpxor     ymm4, ymm4, ymm4                  ; in-set accumulator
        xor       rax, rax
        test      r10, r10
        jz        have0                             ; empty set -> skip compares
in_lp0:
        vpbroadcastw ymm0, word ptr [r9 + rax*2]
        vpcmpeqw  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp0
have0:
        vpor      ymm5, ymm4, ymm3                  ; stop = in-set OR terminator
        vpmovmskb edx, ymm5
        shr       edx, cl                           ; relative to str
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx                          ; byte offset of first stop from str
        shr       edx, 1                            ; -> wchar index
        mov       eax, edx
        vzeroupper
        ret

nextblk:
        add       r11, 32
        vmovdqu   ymm6, ymmword ptr [r11]
        vpcmpeqw  ymm3, ymm6, ymm1
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
        test      r10, r10
        jz        have1
in_lp1:
        vpbroadcastw ymm0, word ptr [r9 + rax*2]
        vpcmpeqw  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp1
have1:
        vpor      ymm5, ymm4, ymm3
        vpmovmskb edx, ymm5
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx                          ; byte offset within block
        lea       rax, [r11 + rdx]                  ; absolute byte address of first stop
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
        jz        ret_have                           ; terminator -> stop (return index)
        xor       rcx, rcx
sc_in:
        movzx     edx, word ptr [r9 + rcx*2]
        test      dx, dx
        jz        sc_adv                             ; end of set, not a member -> keep going
        cmp       dx, r10w
        je        ret_have                           ; in set -> stop (return index)
        inc       rcx
        jmp       sc_in
sc_adv:
        inc       rax
        jmp       sc_next

ret_have:
        vzeroupper
        ret
wia_wcscspn ENDP
END
