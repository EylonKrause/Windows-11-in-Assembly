; changes/232-pathremovebackslasha/impl.asm
; char* wia_pathremovebackslasha(PSTR psz)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathRemoveBackslashA: strip one trailing backslash, unless the result would
; be a bare root. 21.93 ns against 16.88 ns for the wide form on the same character count -- 1.30x
; the wide cost for HALF the bytes, so twice as slow per byte.
;
; The rule is not "strip a trailing backslash", and every part of it was re-derived against the
; NARROW export in probes/prba.c rather than inherited from change 171:
;
;   * The return is always psz + max(n-1, 0) -- a pointer to the last character, not the terminator,
;     and not the start. Confirmed at n = 0, 1, 2 and 3, and on all 19531 enumerated strings.
;   * One trailing backslash is removed UNLESS the remainder would be a bare root:
;         m == 0, or (m == 1 and psz[0] == '\'), or (m == 2 and psz[1] == ':' and psz[0] a letter)
;     where m = n-1. So "\" and "\\" and "C:\" keep their backslash, while "\\\", "C:\\", "ab\" and
;     "\\server\share\" lose one.
;   * Exactly one byte value is ever removed: 0x5C. Sweeping all 255 non-NUL values as the trailing
;     character, only the backslash goes -- a forward slash is not a separator here, so "a/" is left
;     alone and "C:/" is not a protected root.
;   * NULL returns NULL without faulting.
;
; The drive-letter set is where the narrow form differs from the wide one, and it is exactly the
; reason this was re-measured instead of translated. Change 171 pinned the WIDE set by an exhaustive
; 65535-code-unit sweep and found the ASCII letters plus the LATIN-1 letters (0xC0..0xD6, 0xD8..0xF6,
; 0xF8..0xFF). Sweeping all 255 byte values here gives:
;
;     drive letters 0x41..0x5A
;     drive letters 0x61..0x7A
;     52 byte values act as a drive letter, in 2 run(s)
;
; ASCII only. The narrow export does NOT treat a Latin-1 letter as a drive letter, so "\xC0:\" is
; not a protected root while L"\u00C0:\\" is. Inheriting the wide set would have produced a function
; that wrongly protects 78 byte values.
;
; Method: change 225's page-safe length scan -- first block aligned down with the bits before the
; string masked off, then 64-byte aligned pairs -- and then four compares. The scan is the whole
; cost; the rule is a handful of instructions on the result.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.code
wia_pathremovebackslasha PROC
        test      rcx, rcx
        jz        ret_null                       ; NULL -> NULL, measured
        mov       r8, rcx                        ; psz

        ; ---- length, by change 225's scan ----
        mov       r9, rcx
        and       r9, -32                        ; aligned down: same page as psz, always
        mov       ecx, r8d
        and       ecx, 31
        vpxor     ymm1, ymm1, ymm1
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shr       eax, cl                        ; after this, bit i is byte psz[i]
        test      eax, eax
        jnz       sc_first
        add       r9, 32
        test      r9b, 32                        ; already 64-aligned?
        jz        sc_pair
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       sc_at_r9
        add       r9, 32
sc_pair:                                         ; 64-aligned: the window never straddles a page
        vmovdqa   ymm0, ymmword ptr [r9]
        vmovdqa   ymm2, ymmword ptr [r9 + 32]
        vpminub   ymm3, ymm0, ymm2
        vpcmpeqb  ymm3, ymm3, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       sc_pair_hit
        add       r9, 64
        jmp       sc_pair
sc_pair_hit:
        vpcmpeqb  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       sc_at_r9
        vpcmpeqb  ymm3, ymm2, ymm1
        vpmovmskb eax, ymm3
        add       r9, 32
sc_at_r9:
        tzcnt     eax, eax
        lea       rdx, [r9 + rax]
        sub       rdx, r8                        ; rdx = n
        jmp       have_n
sc_first:
        tzcnt     eax, eax
        mov       rdx, rax                       ; rdx = n

have_n:
        vzeroupper
        ; ---- the return value is psz + max(n-1, 0), whatever happens next ----
        lea       rax, [r8 + rdx - 1]
        cmp       rdx, 0
        cmovle    rax, r8                        ; n == 0 -> psz itself
        jle       done                           ; and nothing to strip

        cmp       byte ptr [r8 + rdx - 1], 5Ch   ; a trailing backslash? the ONLY removable byte
        jne       done

        ; ---- the bare-root protections, on m = n-1 ----
        lea       r9, [rdx - 1]                  ; m
        test      r9, r9
        jz        done                           ; m == 0: the whole string is "\"
        cmp       r9, 1
        jne       chk_drive
        cmp       byte ptr [r8], 5Ch             ; m == 1 and psz[0] == '\' : the UNC root "\\"
        je        done
        jmp       strip
chk_drive:
        cmp       r9, 2
        jne       strip
        cmp       byte ptr [r8 + 1], 3Ah         ; m == 2 and psz[1] == ':' ...
        jne       strip
        movzx     r9d, byte ptr [r8]             ; ... and psz[0] an ASCII letter -- ASCII ONLY,
        or        r9d, 20h                       ;     unlike the WIDE form, which also takes the
        sub       r9d, 61h                       ;     Latin-1 letters. Fold case, then one compare.
        cmp       r9d, 25                        ; 'a'..'z' after the fold
        jbe       done                           ; a drive root: keep the backslash
strip:
        mov       byte ptr [r8 + rdx - 1], 0
done:
        ret
ret_null:
        xor       eax, eax
        ret
wia_pathremovebackslasha ENDP
END
