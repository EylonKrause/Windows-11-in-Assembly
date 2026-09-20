; shlwapi.dll!StrChrA  --  hand-written x86-64 reimplementation (11.42x vs shipped)
; source of truth: changes/220-strchra/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/220-strchra/impl.asm
; PSTR wia_strchra(PCSTR pszStart, WORD wMatch)   [Win64: rcx, dx -> rax]
;
; Reimplements shlwapi!StrChrA: the first occurrence of wMatch, or NULL.
;
; ---- This one is a different shape from the rest of the narrow family, and the numbers say so ------
; StrRChrA cost 16.41x its wide sibling, StrCSpnA 13.54x, StrPBrkA 10.03x -- all the signature of an
; MBCS-aware walk with a function call per character. StrChrA costs 1580.19 ns against StrChrW's
; 1565.97 over the same character count: 1.01x the wide cost for HALF the bytes, i.e. only twice the
; cost per byte. That is a plain byte loop, not a CharNextA walk.
;
; So the available ratio here is smaller and comes from vectorisation alone. Change 131 converted
; StrChrW at 7.14x geomean, and a narrow block carries 32 characters to the wide form's 16. This is
; worth having; it is not a 200x target and is not described as one.
;
; ---- what the probe settled (probes/chr.c) ----------------------------------------------------------
;   * BYTE-WISE. Every byte value 0x01..0xFF placed where a lead byte would swallow the character
;     after it: 0 of 254 misbehave (GetACP() is 1252, which has none).
;   * wMatch is a word but only its low byte is consulted -- 0x015A, 0x5A5A and 0xFF5A all find 'Z',
;     and 0x5A00 (low byte NUL) finds nothing.
;   * Searching for the TERMINATOR returns NULL; so does an empty string, and so does a NULL pointer.
;   * The scan STOPS at the terminator: "abc\0Zxy" does not find the 'Z' beyond the embedded NUL.
;   * Every byte value 0x01..0xFF is findable, including 0x80..0xFF, which are ordinary characters
;     on this code page. No length cap.
;
; ---- method ----------------------------------------------------------------------------------------
; One forward pass, two masks per 32-byte block: the target and the terminator. The first set bit of
; either decides the answer -- a target bit below the terminator's is a hit, anything else ends the
; scan.
;
; The first block's mask has the bits BEFORE the string cleared rather than shifted out, so the block
; base can be the signed value -(psz & 31) and every later block is simply +32. That keeps one
; uniform loop instead of a special first iteration.
;
; Page-safe: every load is 32-byte ALIGNED, and a 32-byte aligned load never crosses a page boundary,
; so the scan cannot touch a page the byte-at-a-time export would not have reached.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (shlx). Validated on Zen 4.
;
; Only ymm0-ymm4 are used. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.

.code
wia_strchra PROC
        ; wMatch arrives in DX, not r8w. StrChrA takes TWO arguments; change 213's StrRChrA takes
        ; three, and borrowing its register layout cost one full correctness run in which every
        ; "present" case failed and every "absent" case passed -- the signature of reading the wrong
        ; register for the needle.
        test      dl, dl
        jz        sc_null                    ; seeking the terminator -> NULL. Only the LOW byte of
                                             ; wMatch counts: 0x5A00 finds nothing.
        test      rcx, rcx
        jz        sc_null                    ; measured: NULL in, NULL out

        vmovd     xmm2, edx
        vpbroadcastb ymm2, xmm2
        vpxor     ymm3, ymm3, ymm3
        mov       r11, rcx                   ; the string base

        mov       ecx, r11d
        and       ecx, 31
        mov       r9, r11
        sub       r9, rcx                    ; aligned DOWN: never touches an earlier page

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm2
        vpmovmskb eax, ymm4                  ; the target
        vpcmpeqb  ymm4, ymm0, ymm3
        vpmovmskb edx, ymm4                  ; the terminator
        ; Clear the bits BEFORE the string instead of shifting them out, so the block base can be the
        ; signed value -(psz & 31) and every later block is simply +32 -- one uniform loop.
        mov       r8d, -1
        shlx      r8d, r8d, ecx
        and       eax, r8d
        and       edx, r8d
        mov       r10, rcx
        neg       r10                        ; base of this block, as a signed string index
        jmp       sc_blk

ALIGN 16
sc_next:
        add       r9, 32
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm2
        vpmovmskb eax, ymm4
        vpcmpeqb  ymm4, ymm0, ymm3
        vpmovmskb edx, ymm4
sc_blk:
        test      edx, edx
        jnz       sc_end                     ; the string ends in this block
        test      eax, eax
        jz        sc_next
        tzcnt     ecx, eax
        lea       rax, [r11 + r10]
        add       rax, rcx
        vzeroupper
        ret

sc_end:
        ; Only a match strictly BEFORE the terminator counts.
        tzcnt     ecx, edx
        mov       r8d, 1
        shl       r8d, cl
        dec       r8d
        and       eax, r8d
        jz        sc_null
        tzcnt     ecx, eax
        lea       rax, [r11 + r10]
        add       rax, rcx
        vzeroupper
        ret

sc_null:
        xor       eax, eax
        vzeroupper
        ret
wia_strchra ENDP
END
