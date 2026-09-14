; shlwapi.dll!StrChrW  --  hand-written x86-64 reimplementation (3.67x vs shipped)
; source of truth: changes/131-strchrw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/131-strchrw/impl.asm
; PWSTR wia_strchrw(PCWSTR pszStart, WCHAR wMatch)   [Win64: rcx, dx -> rax]
;
; Reimplements shlwapi!StrChrW. shlwapi's is a scalar one-wchar-at-a-time scan (~1.1 cycles/char --
; 62 ns to scan 254 chars), so this uses the AVX2 block scan proven in the landed 003 wcschr: each
; 32-byte block is compared against BOTH wMatch and 0, and the FIRST stop position decides, so the
; terminator is never scanned past.
;
; It is NOT quite wcschr: searching for wMatch == 0 returns **NULL** here, where C's wcschr returns a
; pointer to the terminator. Verified against the live export.
;
; Page-safe: the first load is aligned down to a 32-byte boundary and the leading bytes are shifted out
; of the mask, and every later load is 32-aligned -- so no load ever crosses into a page the string does
; not already occupy.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strchrw PROC
        test      dx, dx
        jz        sc_null                          ; StrChrW(s, 0) -> NULL (diverges from wcschr)
        vmovd     xmm2, edx
        vpbroadcastw ymm2, xmm2                    ; ymm2 = wMatch broadcast
        vpxor     ymm1, ymm1, ymm1
        mov       r11, rcx                         ; keep s
        mov       r9, rcx
        and       r9, -32                          ; aligned-down base
        mov       ecx, r11d
        and       ecx, 31                          ; byte offset of the start within that block

        vpcmpeqw  ymm0, ymm2, ymmword ptr [r9]     ; == wMatch
        vpcmpeqw  ymm3, ymm1, ymmword ptr [r9]     ; == 0
        vpor      ymm4, ymm0, ymm3                 ; stop = match or terminator
        vpmovmskb r8d, ymm4
        vpmovmskb edx, ymm0
        shr       r8d, cl                          ; drop the bytes before the string start
        shr       edx, cl
        test      r8d, r8d
        jz        sc_scan
        tzcnt     r8d, r8d                         ; first stop, byte offset from s
        bt        edx, r8d                         ; was that stop a match, or the terminator?
        jnc       sc_null
        lea       rax, [r11 + r8]
        vzeroupper
        ret
sc_scan:
        add       r9, 32
        vpcmpeqw  ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm1, ymmword ptr [r9]
        vpor      ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4
        test      r8d, r8d
        jz        sc_scan
        vpmovmskb edx, ymm0
        tzcnt     r8d, r8d
        bt        edx, r8d
        jnc       sc_null
        lea       rax, [r9 + r8]
        vzeroupper
        ret
sc_null:
        xor       eax, eax
        vzeroupper
        ret
wia_strchrw ENDP
END
