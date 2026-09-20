; ucrtbase.dll!wcsrchr  --  hand-written x86-64 reimplementation (1.82x vs shipped)
; source of truth: changes/149-wcsrchr/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/149-wcsrchr/impl.asm
; wchar_t* wia_wcsrchr(const wchar_t* s, wchar_t c)   [Win64: rcx, dx -> rax]
;
; Reimplements ucrtbase!wcsrchr: last occurrence of c, else NULL. ucrtbase's is scalar (~0.117 ns/char --
; 29.8 ns for 254 chars, 238 ns for 2048), and notably its NARROW sibling strrchr is already vectorised
; (11.8 ns for the same 254 characters). The wide one was left behind, exactly as with wcstok_s
; (change 148) versus strtok_s.
;
; Contract (probed against the live export):
;   - returns the LAST match, or NULL;
;   - c == 0 returns a pointer to the TERMINATOR ("abc" -> s+3, "" -> s+0). That is standard C and the
;     opposite of shlwapi's StrRChrW (change 134), which returns NULL for a NUL search, two functions
;     doing "reverse character search" that disagree on this exact case.
;
; Method: a single FORWARD pass tracking the last match. Scanning forward rather than backward avoids
; needing the length first: a backward scan would have to locate the terminator anyway, costing a whole
; extra pass over the string. Per 32-byte block the match and terminator masks are taken; if the
; terminator is in this block the match mask is clipped to the bytes before it and the scan ends.
;
; Page-safe: the first load is aligned down to 32 bytes with the leading bytes shifted out of the mask,
; and every later load is 32-aligned, so no load crosses into a page the string does not occupy.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_wcsrchr PROC
        vpxor     ymm3, ymm3, ymm3
        mov       r11, rcx                          ; position base for this block
        mov       r9, rcx
        and       r9, -32
        mov       r8d, ecx
        and       r8d, 31                           ; byte offset of the string within the block
        test      dx, dx
        jz        rc_nul                            ; searching for NUL -> return the terminator

        vmovd     xmm2, edx
        vpbroadcastw ymm2, xmm2
        xor       eax, eax                          ; last match = NULL

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm2
        vpmovmskb edx, ymm1                         ; matches
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb r10d, ymm1                        ; terminator
        mov       ecx, r8d
        shr       edx, cl
        shr       r10d, cl
        jmp       rc_blk
rc_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm2
        vpmovmskb edx, ymm1
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb r10d, ymm1
rc_blk:
        test      r10d, r10d
        jz        rc_upd
        ; terminator in this block: keep only matches before it, then finish
        tzcnt     ecx, r10d
        mov       r10d, 1
        shl       r10d, cl
        dec       r10d
        and       edx, r10d
        test      edx, edx
        jz        rc_done
        bsr       ecx, edx
        and       ecx, -2                           ; vpcmpeqw sets both bytes of a matching word
        lea       rax, [r11 + rcx]
rc_done:
        vzeroupper
        ret
rc_upd:
        test      edx, edx
        jz        rc_next
        bsr       ecx, edx
        and       ecx, -2
        lea       rax, [r11 + rcx]                  ; remember the last match in this block
        jmp       rc_next

        ; ---- c == 0: locate the terminator and return it ----
rc_nul:
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb edx, ymm1
        mov       ecx, r8d
        shr       edx, cl
        test      edx, edx
        jnz       rc_nul_here
rc_nul_loop:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb edx, ymm1
        test      edx, edx
        jz        rc_nul_loop
rc_nul_here:
        tzcnt     edx, edx
        lea       rax, [r11 + rdx]
        vzeroupper
        ret
wia_wcsrchr ENDP
END
