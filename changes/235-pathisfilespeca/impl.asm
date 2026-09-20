; changes/235-pathisfilespeca/impl.asm
; BOOL wia_pathisfilespeca(PCSTR psz)   [Win64: rcx -> eax]
;
; Reimplements shlwapi!PathIsFileSpecA: is this a bare file name, with no path separator in it?
; 4.38 ns against 1.57 ns for the wide form on the same character count -- 2.79x the wide cost for
; HALF the bytes. The last of the twelve narrow siblings in discovery/shlwapi_narrow2.c, and the
; smallest in absolute terms: it writes nothing, returns a BOOL, and the whole job is one scan.
;
; THE CONTRACT, measured in probes/pifsa.c:
;
;   * Exactly two byte values are separators: 0x5C and 0x3A. Confirmed at the first, middle and last
;     positions -- 2 of 255 at each -- so neither is position-dependent. a forward slash is not one:
;     "a/b" and "/" are both TRUE.
;   * The empty string is TRUE. That is the one case a natural model gets wrong, and it was the only
;     mismatch in 488281 enumerated strings when this probe first ran with "non-empty" in its rule.
;     It is not a special case in the code either -- a string with no characters trivially contains
;     no separator, so the scan falls straight through to TRUE.
;   * NULL returns 0.
;   * 0 mismatches over all 488281 strings of {a, backslash, :, /, 0x80} to length 8.
;
; Method: one forward pass looking for any of three bytes -- the terminator, 0x5C, 0x3A -- as three
; vpcmpeqb and two vpor per 32-byte block, so a single extraction answers all of them. If the first
; one found is the terminator the answer is TRUE; otherwise it is FALSE. There is no second pass and
; no length: the function never needs to know how long the string is.
;
; Page safety: every 32-byte load is issued only when (cursor & 4095) <= 4064, proving the read
; stays inside the cursor's own page -- necessarily mapped, since the bytes already scanned came
; from it. Within 32 bytes of a page end it steps one byte and retries. probes/pifsa.c confirms the
; shipped export does not overread either, in both shapes: 398 of 398 guard-page cases were clean.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_bs32  db 32 dup(05Ch)                  ; backslash
c_cl32  db 32 dup(03Ah)                  ; colon -- the other separator, and the one a reader forgets

.code
wia_pathisfilespeca PROC
        test      rcx, rcx
        jz        ret_false                      ; NULL -> 0, measured
        mov       r9, rcx                        ; cursor
        vpxor     ymm1, ymm1, ymm1

scan:
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4064                      ; a 32-byte read must stay inside this page
        ja        scan1
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm2, ymm0, ymm1               ; == terminator
        vpcmpeqb  ymm3, ymm0, ymmword ptr [c_bs32]
        vpcmpeqb  ymm4, ymm0, ymmword ptr [c_cl32]
        vpor      ymm3, ymm3, ymm4               ; either separator
        vpor      ymm2, ymm2, ymm3               ; ... or the end: one extraction decides
        vpmovmskb eax, ymm2
        test      eax, eax
        jz        blk_next
        tzcnt     eax, eax
        cmp       byte ptr [r9 + rax], 0         ; whichever came first -- was it the terminator?
        jne       ret_false                      ; no: a separator, so this is not a file spec
        mov       eax, 1
        vzeroupper
        ret
blk_next:
        add       r9, 32
        jmp       scan

scan1:                                           ; one byte, then retry the vector path
        movzx     eax, byte ptr [r9]
        test      al, al
        jz        ret_true
        cmp       al, 5Ch
        je        ret_false
        cmp       al, 3Ah
        je        ret_false
        inc       r9
        jmp       scan
ret_true:
        mov       eax, 1
        vzeroupper
        ret
ret_false:
        xor       eax, eax
        vzeroupper
        ret
wia_pathisfilespeca ENDP
END
