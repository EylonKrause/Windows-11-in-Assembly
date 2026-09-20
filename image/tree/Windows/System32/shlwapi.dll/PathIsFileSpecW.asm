; shlwapi.dll!PathIsFileSpecW  --  hand-written x86-64 reimplementation (5.73x vs shipped)
; source of truth: changes/138-pathisfilespecw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/138-pathisfilespecw/impl.asm
; BOOL wia_pathisfilespecw(PCWSTR pszPath)   [Win64: rcx -> eax]
;
; Reimplements shlwapi!PathIsFileSpecW: TRUE when the string contains no path-delimiting character.
; shlwapi's is a scalar scan (119 ns for 254 chars).
;
; Contract (probed against the live export): FALSE iff the string contains ':' (003A) or '\' (005C),
; at ANY position. A forward slash does NOT disqualify, "/abc" is a file spec while "\abc" is not.
; The empty string is TRUE.
;
; That makes this the THIRD separator convention in this one DLL: PathFindExtensionW (change 132) stops
; only at '\' and ignores '/' and ':'; PathFindFileNameW treats all three as separators; and this one
; treats ':' and '\' but not '/'. Each was measured, not assumed.
;
; Method: the 003/131 dual-compare block scan extended to two "bad" characters; each 32-byte block is
; compared against ':', '\' and 0, and the FIRST stop decides: a bad character returns FALSE, the
; terminator returns TRUE. Page-safe: masked aligned prologue, all later loads 32-aligned.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
ALIGN 16
c_colon dw 003Ah
c_bslsh dw 005Ch

.code
wia_pathisfilespecw PROC
        vpbroadcastw ymm1, word ptr c_colon
        vpbroadcastw ymm2, word ptr c_bslsh
        vpxor     ymm3, ymm3, ymm3
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm1
        vpmovmskb r8d, ymm4
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb edx, ymm4
        or        r8d, edx                          ; bad = ':' or '\'
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb eax, ymm4                         ; terminator
        or        eax, r8d                          ; stop = bad or terminator
        shr       eax, cl                           ; drop bytes before the string start
        shr       r8d, cl
        test      eax, eax
        jz        fs_next
        tzcnt     eax, eax
        bt        r8d, eax                          ; first stop: bad character?
        jc        fs_false
        mov       eax, 1
        vzeroupper
        ret

fs_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm1
        vpmovmskb r8d, ymm4
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb edx, ymm4
        or        r8d, edx
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb eax, ymm4
        or        eax, r8d
        test      eax, eax
        jz        fs_next
        tzcnt     eax, eax
        bt        r8d, eax
        jc        fs_false
        mov       eax, 1
        vzeroupper
        ret
fs_false:
        xor       eax, eax
        vzeroupper
        ret
wia_pathisfilespecw ENDP
END
