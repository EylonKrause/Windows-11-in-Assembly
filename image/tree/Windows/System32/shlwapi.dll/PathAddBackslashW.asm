; shlwapi.dll!PathAddBackslashW  --  hand-written x86-64 reimplementation (8.39x vs shipped)
; source of truth: changes/142-pathaddbackslashw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/142-pathaddbackslashw/impl.asm
; PWSTR wia_pathaddbackslashw(PWSTR pszPath)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathAddBackslashW: append a trailing backslash if the path lacks one. shlwapi's
; cost is essentially its scalar length scan (69 ns for a 254-char path).
;
; Contract (probed against the live export):
;   - already ends with '\'  -> unchanged, returns a pointer to the terminator;
;   - a FORWARD slash does not count ("a/" becomes "a/\");
;   - the EMPTY string is left alone -- nothing is appended, and psz is returned;
;   - MAX_PATH guard: it appends only while the result still fits in 259 characters. Measured at the
;     boundary -- length 258 appends (giving 259), length 259 returns **NULL** and changes nothing.
;     Note this differs again from its neighbours: change 140 silently does nothing past the limit,
;     change 141 has no limit at all, and this one reports failure by returning NULL;
;   - otherwise appends '\' + NUL and returns a pointer to the new terminator.
;
; Method: AVX2 scan for the terminator (page-safe: masked aligned prologue, all later loads 32-aligned),
; then a couple of compares. The length scan is the whole job, so vectorising it is the whole win.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_pathaddbackslashw PROC
        mov       r11, rcx                          ; psz
        vpxor     ymm3, ymm3, ymm3
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        shr       eax, cl
        test      eax, eax
        jnz       ab_here
ab_loop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        ab_loop
        tzcnt     eax, eax
        add       rax, r9
        sub       rax, r11
        shr       rax, 1
        jmp       ab_len
ab_here:
        tzcnt     eax, eax
        shr       eax, 1
ab_len:
        mov       r8, rax                           ; length in characters
        test      r8, r8
        jz        ab_same                           ; empty string -> return psz unchanged
        cmp       word ptr [r11 + r8*2 - 2], 5Ch
        jne       ab_append
        ; Already ends with a backslash. The MAX_PATH test still applies and is checked FIRST: the
        ; rule is "does the RESULT, terminator included, fit in 260 characters?" -- so a path that
        ; already ends with '\' still fails once len >= 260, even though nothing would be written.
        cmp       r8, 260
        jae       ab_null
        jmp       ab_same
ab_append:
        cmp       r8, 259                           ; appending needs len + 2 <= 260
        jae       ab_null
        mov       word ptr [r11 + r8*2], 5Ch        ; append '\'
        mov       word ptr [r11 + r8*2 + 2], 0
        lea       rax, [r11 + r8*2 + 2]             ; pointer to the new terminator
        vzeroupper
        ret
ab_same:
        lea       rax, [r11 + r8*2]                 ; pointer to the existing terminator
        vzeroupper
        ret
ab_null:
        xor       eax, eax
        vzeroupper
        ret
wia_pathaddbackslashw ENDP
END
