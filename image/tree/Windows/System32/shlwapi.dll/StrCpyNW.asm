; shlwapi.dll!StrCpyNW  --  hand-written x86-64 reimplementation (5.01x vs shipped)
; source of truth: changes/168-strcpynw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/168-strcpynw/impl.asm
; PWSTR wia_strcpynw(PWSTR dst, PCWSTR src, int cchMax)
;   [Win64: rcx, rdx, r8d -> rax (returns dst)]
;
; Reimplements shlwapi!StrCpyNW: copy at most cchMax-1 wide characters from src to dst and
; terminate. shlwapi's is a scalar character-at-a-time loop -- 108 ns to copy a 254-char
; path, i.e. ~4.7 GB/s.
;
; Contract (derived in probes/scn.c, fuzz-confirmed bit-exact against the live export over
; 2,000,000 cases):
;   - cchMax <= 0  -> writes nothing at all (not even a terminator) and returns dst. The
;                     negative case matters: cchMax is a signed int and is compared signed,
;                     so -1 does not mean "huge".
;   - otherwise    -> copies min(cchMax-1, wcslen(src)) characters, then exactly ONE NUL.
;                     It is strlcpy-shaped, NOT strncpy-shaped: there is no NUL fill of the
;                     remainder, and nothing past the terminator is touched.
;   - returns dst always.
;
; Method: one fused scan-and-copy pass. Each iteration loads 32 bytes of src, tests for a
; terminator with vpcmpeqw, and -- only if the block is terminator-free and the remaining
; budget is at least 16 characters -- stores all 32 bytes to dst. The moment a terminator is
; in range, or the budget drops below 16, the scalar tail finishes the job. So the string is
; traversed ONCE; there is no separate wcslen pass.
;
; Page safety:
;   * The 32-byte source load is issued only when (src & 4095) <= 4064, which proves the read
;     stays inside src's own page -- a page that must be mapped, because the characters we
;     have already copied came from it. Within 32 bytes of a page end the code copies a
;     single character and re-tests, so it slides across the boundary scalar-wise and then
;     resumes vector speed, rather than degrading to scalar for the whole string.
;   * The 32-byte destination store is issued only while the remaining budget is >= 16
;     characters, so it can never write beyond cchMax-1 characters of dst.
;
; ISA: AVX2. No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.code
wia_strcpynw PROC
        mov       rax, rcx                       ; return value = dst (always)
        test      r8d, r8d
        jle       rp                             ; cchMax <= 0 -> write nothing at all
        movsxd    r8, r8d
        dec       r8                             ; budget = cchMax - 1 characters
        mov       r9, rcx                        ; dst cursor
        mov       r10, rdx                       ; src cursor
        test      r8, r8
        jz        nul_s                          ; cchMax == 1 -> terminator only

        vpxor     ymm1, ymm1, ymm1
blk:
        cmp       r8, 16
        jb        tail                           ; fewer than 16 chars of budget left
        mov       r11d, r10d
        and       r11d, 4095
        cmp       r11d, 4064                     ; 32-byte read must stay inside this page
        ja        step1                          ; near a page end -> creep one char
        vmovdqu   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r11d, ymm2
        test      r11d, r11d
        jnz       tail                           ; terminator within these 16 -> scalar finish
        vmovdqu   ymmword ptr [r9], ymm0         ; 16 characters at a time
        add       r10, 32
        add       r9, 32
        sub       r8, 16
        jmp       blk

step1:                                           ; one character, then retry the vector path
        movzx     r11d, word ptr [r10]
        test      r11w, r11w
        jz        nul_v
        mov       word ptr [r9], r11w
        add       r10, 2
        add       r9, 2
        dec       r8
        jnz       blk
        jmp       nul_v

tail:                                            ; <= 16 characters remain, or budget is small
        test      r8, r8
        jz        nul_v
t_lp:
        movzx     r11d, word ptr [r10]
        test      r11w, r11w
        jz        nul_v
        mov       word ptr [r9], r11w
        add       r10, 2
        add       r9, 2
        dec       r8
        jnz       t_lp

nul_v:
        mov       word ptr [r9], 0               ; exactly one terminator
        vzeroupper
        ret

nul_s:                                           ; scalar-only path: no YMM was touched
        mov       word ptr [r9], 0
rp:
        ret
wia_strcpynw ENDP
END
