; changes/164-pathcchaddbackslash/impl.asm
; HRESULT wia_pathcchaddbackslash(PWSTR pszPath, size_t cchPath)   [Win64: rcx, rdx -> eax]
;
; Reimplements kernelbase!PathCchAddBackslash: append a backslash unless the path already ends with
; one. 31 ns for a ~70-character path -- almost all of it a character-at-a-time bounded strlen.
;
; Contract (probed against the live export). The ORDER of the checks is observable and was measured:
;   1. cch == 0, or the path is not NUL-terminated within cch
;                                        -> STRSAFE_E_INSUFFICIENT_BUFFER (0x8007007A), untouched;
;   2. the path is empty, or already ends with '\'
;                                        -> S_FALSE (0x00000001), untouched;
;   3. no room for the extra character   -> 0x8007007A, untouched;
;   4. otherwise                         -> the backslash and a terminator are appended, S_OK.
;
; Step 2 sits BETWEEN the two size checks, which a single "validate then act" reading would get
; wrong in both directions. Probed: "C:\a\" with cch = 3 returns 0x8007007A (the termination check
; wins), while the same path with cch = 6 -- too small to hold a sixth character -- returns S_FALSE,
; because by then the trailing backslash has already settled it.
;
; Three things this function does NOT do, each checked rather than assumed:
;   * no PATHCCH_MAX_CCH ceiling -- cch = 32769 is accepted, unlike changes 159 and 160;
;   * no MAX_PATH limit -- a 261-character result is fine;
;   * no NULL check -- pszPath = NULL FAULTS rather than returning E_INVALIDARG, so dereferencing it
;     reproduces the behaviour exactly.
; A trailing '/' does not count as a backslash: "a/" becomes "a/\".
;
; ---- why a short path never touches a vector register --------------------------------------------
; The first working version was 0.89x at 16 characters: correct, but slower than the live scalar
; loop. Two costs, both avoidable and both only material when the string is short. The vector
; prologue is ~15 cycles of load -> compare -> vpmovmskb -> shrx -> tzcnt before anything is known,
; and it ends in a vzeroupper; and a caller that has just written the buffer -- which any caller of
; an in-place path routine has -- leaves stores that a 32-byte load over the same bytes cannot
; forward from, while a 2-byte load forwards cleanly. Same effect as changes 152 and 156.
;
; So paths of 23 characters or fewer are measured by a plain scalar probe and never reach a vector
; register at all. The probe is entered only when cch >= 24, which is what makes reading those 24
; characters safe: the caller has promised that many. Anything longer falls through to the bounded
; AVX2 scan, where the setup is amortised over a string worth scanning.
;
; cch is doubled to a byte bound with the saturating shl/sbb/or from change 151. That matters here
; precisely because there is no PATHCCH_MAX_CCH check to lean on: a caller may legitimately pass a
; huge cch, and a plain shift would wrap it to a small bound and produce a bogus 0x8007007A.
;
; ISA: AVX2 + BMI2 (shrx). Validated on Zen3.

.code
wia_pathcchaddbackslash PROC
        test      rdx, rdx
        jz        ab_insuf

        mov       r11, rcx                          ; path
        mov       r10, rdx                          ; cch, in characters

        ; ---- short paths: no vector state, and 2-byte loads that forward -----------------------
        cmp       rdx, 24
        jb        ab_vector                         ; fewer than 24 characters promised
        xor       eax, eax
ab_probe:
        cmp       word ptr [r11 + rax*2], 0
        je        ab_check                          ; length is in rax, and rax <= 23 < cch
        inc       eax
        cmp       eax, 24
        jb        ab_probe
        ; longer than 23 characters: fall through to the vector scan

ab_vector:
        shl       rdx, 1                            ; -> bytes, saturating, because cch is unbounded
        sbb       rax, rax
        or        rdx, rax
        vpxor     ymm1, ymm1, ymm1
        mov       r9, r11
        and       r9, -32
        mov       ecx, r11d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, ecx
        neg       ecx
        add       ecx, 32
        test      eax, eax
        jnz       ab_lo
ab_next:
        cmp       rcx, rdx
        jae       ab_insuf_v                        ; not terminated within cch
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       ab_hi
        add       rcx, 32
        jmp       ab_next
ab_hi:  tzcnt     eax, eax
        add       rax, rcx
        jmp       ab_have
ab_lo:  tzcnt     eax, eax
ab_have:
        cmp       rax, rdx
        jae       ab_insuf_v                        ; the NUL is at or past the bound
        shr       rax, 1                            ; length, in characters
        vzeroupper

ab_check:
        ; ---- already ends with a backslash, or empty -> S_FALSE, before the room check -----------
        test      rax, rax
        jz        ab_sfalse
        cmp       word ptr [r11 + rax*2 - 2], 005Ch
        je        ab_sfalse

        ; ---- room for one more character? -------------------------------------------------------
        lea       rcx, [rax + 2]
        cmp       rcx, r10
        ja        ab_insuf

        mov       word ptr [r11 + rax*2], 005Ch     ; the backslash
        mov       word ptr [r11 + rax*2 + 2], 0     ; and the terminator
        xor       eax, eax                          ; S_OK
        ret

ab_sfalse:
        mov       eax, 1                            ; S_FALSE
        ret
ab_insuf_v:
        vzeroupper
ab_insuf:
        mov       eax, 8007007Ah                    ; STRSAFE_E_INSUFFICIENT_BUFFER
        ret
wia_pathcchaddbackslash ENDP
END
