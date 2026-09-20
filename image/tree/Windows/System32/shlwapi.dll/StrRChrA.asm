; shlwapi.dll!StrRChrA  --  hand-written x86-64 reimplementation (149.12x vs shipped)
; source of truth: changes/213-strrchra/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/213-strrchra/impl.asm
; Pstr wia_strrchra(PCSTR pszStart, pcstr pszEnd, word wMatch)   [Win64: rcx, rdx, r8w -> rax]
;
; Reimplements shlwapi!StrRChrA, which carries the biggest ratio the narrow survey found: 14351.08 ns
; to search 4000 characters against 874.77 ns for StrRChrW over the same character count. SIXTEEN
; TIMES the wide cost for HALF the bytes is thirty-two times the cost per byte.
;
; ---- what the probe found, and why the number is what it is -----------------------------------------
; probes/srca.c pinned the shipped algorithm exactly, and one observation did most of the work: an
; pszEnd placed past the string's terminator makes the live export never RETURN. Two different inputs
; did it -- "abc" with pszEnd = s+4, and "abc\0ZZZZ\0" with pszEnd = t+9 -- and the first cost a
; 300-second timeout to locate. That pins the loop:
;
;     last = NULL;  p = pszStart;
;     while (p != end) { if (*p == (char)wMatch) last = p;  p = CharNextA(p); }
;
; CharNextA does not advance past a terminator -- it returns the same pointer -- so when `end` lies
; beyond the NUL the walk can never reach it and spins. Every other measurement falls out of that one
; LOOP: pszEnd is EXCLUSIVE because the test is `p != end` before the body (pszEnd = s+7, sitting on
; a match, finds the PREVIOUS one); searching for the TERMINATOR always returns NULL because a valid
; range stops at or before the NUL and so never contains one; and the cost is what it is because
; CharNextA is a function call per character.
;
; CONTRACT DOMAIN, therefore: pszEnd == NULL (search to the terminator), or
; pszStart <= pszEnd <= pszStart+strlen. Outside it the export does not return. A hang is not
; behaviour a caller can depend on and is NOT reproduced here -- this returns an answer instead.
; That is a deliberate, documented divergence on inputs where the shipped function produces no
; result at all, and correctness.c stays inside the domain.
;
; And that domain is narrower than the wide form's. Change 134 recorded, verified, that StrRChrW
; searches the RAW range when given an explicit pszEnd -- ignoring embedded NULs and running past the
; terminator if asked. The A form cannot, because CharNextA is in its loop. Two functions with the
; same name and different domains; one more reason this project re-probes every A form instead of
; inheriting its W.
;
; Two more facts, measured:
;   * BYTE-WISE on this code page. Every byte value 0x01..0xFF was placed where a lead byte would
;     swallow the character after it: ZERO of 255 behave as one (GetACP() is 1252, which has none).
;     A vector scan reproduces this exactly.
;   * wMatch is a word but only its low byte is consulted -- 0x015A, 0x5A5A and 0xFF5A all find 'Z',
;     and 0x5A00 (low byte NUL) finds nothing.
;
; ---- method ----------------------------------------------------------------------------------------
; Two AVX2 paths, the shape change 134 established for the wide form. The bounded form scans BACKWARD
; from the end so it exits at the first match found (the common "last separator in a path" use); the
; unbounded form scans forward tracking the last match, because finding the terminator first would
; cost a whole extra pass. All loads are 32-byte ALIGNED, and a 32-byte aligned load never crosses a
; page boundary, so page safety is structural -- the range ends are handled by masking bits out of
; the compare result, never by narrowing the load.
;
; A narrow block carries 32 positions to the wide form's 16, and vpcmpeqb sets ONE mask bit per match
; rather than a pair, so the `and ecx, -2` that 134 needs after every bsr disappears here.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen 4.
;
; Only ymm0-ymm4 are used. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.

.code
wia_strrchra PROC
        test      r8b, r8b
        jz        rc_null                           ; seeking the terminator -> NULL. Only the LOW
                                                    ; byte of wMatch counts: 0x5A00 finds nothing.
        test      rcx, rcx
        jz        rc_null                           ; measured: NULL start -> NULL
        vmovd     xmm2, r8d
        vpbroadcastb ymm2, xmm2
        mov       r11, rcx                          ; start
        test      rdx, rdx
        jz        rc_fwd
        cmp       rdx, rcx
        jbe       rc_null                           ; end <= start: the range is empty

        ; ---- bounded: backward over [start, end) ----
        ; Only the first and last blocks need masking, so neither test is in the loop. The first cut
        ; recomputed both end-masks on every block and measured 10.02 ns on a 254-character bounded
        ; miss -- SLOWER than the 5.11 ns the unbounded path took over the same string, which does
        ; two compares per block instead of one. That is the giveaway that the cost was bookkeeping,
        ; not work. The loop below is eight instructions with no masking in it at all.
rc_bk:
        mov       r8, r11
        and       r8, -32                           ; base of the block holding `start`
        lea       r9, [rdx - 1]
        and       r9, -32                           ; base of the block holding the last byte

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm2
        vpmovmskb eax, ymm4
        mov       r10, rdx
        sub       r10, r9                           ; bytes of this block below `end`, 1..32
        cmp       r10, 32
        jae       rc_hi_ok
        mov       ecx, r10d
        mov       r10d, 1
        shl       r10d, cl
        dec       r10d
        and       eax, r10d                         ; drop bytes at/after end
rc_hi_ok:
        cmp       r9, r8
        jne       rc_top_done                       ; `start` lies in a lower block
        ; start and end share one block: mask below start too, and this is the whole search
        mov       r10, r11
        sub       r10, r9
        mov       ecx, r10d
        mov       r10d, -1
        shl       r10d, cl                          ; r10 == 0 gives -1, i.e. no masking
        and       eax, r10d
        test      eax, eax
        jz        rc_null
        bsr       ecx, eax
        lea       rax, [r9 + rcx]
        vzeroupper
        ret
rc_top_done:
        test      eax, eax
        jnz       rc_hit

ALIGN 16
rc_mid:
        sub       r9, 32
        cmp       r9, r8
        je        rc_bottom
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm2
        vpmovmskb eax, ymm4
        test      eax, eax
        jz        rc_mid
rc_hit:
        bsr       ecx, eax                          ; one bit per byte here, so no rounding
        lea       rax, [r9 + rcx]
        vzeroupper
        ret

rc_bottom:
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm2
        vpmovmskb eax, ymm4
        mov       r10, r11
        sub       r10, r9                           ; bytes of this block below `start`, 0..31
        mov       ecx, r10d
        mov       r10d, -1
        shl       r10d, cl
        and       eax, r10d                         ; drop bytes before start
        test      eax, eax
        jz        rc_null
        jmp       rc_hit

        ; ---- unbounded: forward, tracking the last match, stopping at the terminator ----
rc_fwd:
        vpxor     ymm3, ymm3, ymm3
        xor       eax, eax                          ; last match = NULL
        mov       r9, rcx
        and       r9, -32
        mov       ecx, r11d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm2
        vpmovmskb edx, ymm4
        vpcmpeqb  ymm4, ymm0, ymm3
        vpmovmskb r10d, ymm4
        shr       edx, cl                           ; positions now relative to r11 = start
        shr       r10d, cl
        jmp       rc_f_blk
ALIGN 16
rc_f_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm2
        vpmovmskb edx, ymm4
        vpcmpeqb  ymm4, ymm0, ymm3
        vpmovmskb r10d, ymm4
rc_f_blk:
        test      r10d, r10d
        jz        rc_f_upd
        tzcnt     ecx, r10d                         ; terminator in this block
        mov       r10d, 1
        shl       r10d, cl
        dec       r10d
        and       edx, r10d                         ; keep only matches before it
        test      edx, edx
        jz        rc_f_done
        bsr       ecx, edx
        lea       rax, [r11 + rcx]
rc_f_done:
        vzeroupper
        ret
rc_f_upd:
        test      edx, edx
        jz        rc_f_next
        bsr       ecx, edx
        lea       rax, [r11 + rcx]
        jmp       rc_f_next
rc_null:
        xor       eax, eax
        vzeroupper
        ret
wia_strrchra ENDP
END
