; shlwapi.dll!StrPBrkA  --  hand-written x86-64 reimplementation (167.54x vs shipped)
; source of truth: changes/215-strpbrka/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/215-strpbrka/impl.asm
; PSTR wia_strpbrka(PCSTR pszStr, PCSTR pszSet)   [Win64: rcx, rdx -> rax]
;
; Reimplements shlwapi!StrPBrkA: a pointer to the first character of pszStr that IS in pszSet, or
; NULL if none is. The live export costs 23808.12 ns on 4000 characters against 2374.10 ns for
; StrPBrkW over the same character count, 10.03x the wide cost for HALF the bytes, the MBCS-walk
; signature this project has now seen across the whole narrow shlwapi family.
;
; This is change 214's core with one different ending, and the two were written together: the set
; bitmap, the two-table vpshufb membership test and the page-safe aligned scan are identical, and
; only the answer differs. 214 returns the INDEX of the first stop; this returns a POINTER to it,
; or NULL when the stop was the terminator rather than a member.
;
; ---- what the probe settled (probes/span.c) ---------------------------------------------------------
;   * BYTE-WISE, and so are its two siblings. Every byte value 0x01..0xFF was placed where a lead byte
;     would swallow the character after it: 0 of 254 misbehave for StrCSpnA, StrPBrkA and StrSpnA
;     alike. The SET string is byte-wise too (0 of 252 values cannot be a member) so any byte can
;     belong to the set and a 256-bit membership test reproduces all of it exactly.
;   * For THIS export both degenerate sets happen to agree, StrPBrkA("abc", NULL) and
;     StrPBrkA("abc", "") are both NULL, unlike its sibling StrCSpnA, where a NULL set returns 0
;     and an EMPTY set returns strlen. The distinction is still measured rather than assumed,
;     because the two functions share a core and it would be easy to carry the wrong rule across.
;   * NULL subject -> NULL. Empty subject -> NULL. Duplicates in the set are harmless. No length cap.
;
; ---- the observation that makes this cheap ----------------------------------------------------------
; The set string is NUL-TERMINATED, so the set can never contain a NUL, so the subject's own
; terminator is never a member. One mask of "set member OR terminator" therefore finds the stop for
; the whole family in a single scan, and because a NUL can never be a member, one test of the byte
; At the stop separates the two outcomes: a NUL means no member exists and the answer is NULL,
; anything else is the member itself.
;
; ---- method ----------------------------------------------------------------------------------------
; The set becomes a 256-BIT bitmap in the caller's shadow space, which is 32 bytes, exactly the
; size of the bitmap, and is ours to use, so nothing is pushed and no frame is set up. Each set
; character sets bit b of that region: byte b>>3, bit b&7.
;
; Membership for 32 characters at once is then the standard two-table vpshufb test, and the bitmap's
; natural layout is exactly what it wants:
;     idx   = (v >> 3) & 15, which bitmap byte, within a 16-byte half
;     rows  = vpshufb(tabL, idx) or vpshufb(tabH, idx), selected by v's BIT 7 (i.e. v >= 128,
;             i.e. bitmap byte >= 16) using vpblendvb, which keys on exactly that bit
;     bits  = vpshufb(POW2, v & 7)
;     member = (rows & bits) == bits
; That is why the bitmap is indexed bit-per-byte-value rather than in the nibble-indexed layout the
; same trick usually uses: this way the BUILD is a handful of simple ops per set character instead
; of nine, and the test costs the same.
;
; Page-safe: the first load is aligned DOWN to 32 bytes with the leading bytes shifted out of the
; mask, and every later load is 32-aligned, so no load touches a page the byte-at-a-time export
; would not have reached. Garbage before the string cannot produce a false hit because those bits
; are shifted away before the mask is tested.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (shrx). Validated on Zen 4.
;
; Only ymm0-ymm5 are used. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.

.const
ALIGN 16
c_0F    db 32 dup(0Fh)
c_07    db 32 dup(007h)
c_zero  db 32 dup(000h)
; 1 << i for i = 0..7; indices 8..15 are never used, because (v & 7) <= 7
c_pow2  db 001h,002h,004h,008h,010h,020h,040h,080h, 0,0,0,0,0,0,0,0

.code

; Build, in eax, the mask of positions in ymm0 that are a set member OR the terminator.
; Clobbers ymm4, ymm5. Reads ymm1 = tabL, ymm2 = tabH, ymm3 = POW2.
CLASSIFY MACRO
        vpsrlw    ymm4, ymm0, 3
        vpand     ymm4, ymm4, ymmword ptr [c_0F]       ; bitmap byte index within a 16-byte half
        vpshufb   ymm5, ymm2, ymm4                     ; rows from the HIGH half
        vpshufb   ymm4, ymm1, ymm4                     ; rows from the LOW half
        vpblendvb ymm4, ymm4, ymm5, ymm0               ; v's bit 7 selects the half
        vpand     ymm5, ymm0, ymmword ptr [c_07]
        vpshufb   ymm5, ymm3, ymm5                     ; 1 << (v & 7)
        vpand     ymm4, ymm4, ymm5
        vpcmpeqb  ymm4, ymm4, ymm5                     ; set member
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_zero]     ; the terminator
        vpor      ymm4, ymm4, ymm5
        vpmovmskb eax, ymm4
ENDM

wia_strpbrka PROC
        test      rcx, rcx
        jz        pb_zero
        test      rdx, rdx
        jz        pb_zero                    ; measured: either NULL argument returns NULL

        ; ---- the set becomes a 256-bit bitmap in the caller's shadow space ----
        vpxor     xmm0, xmm0, xmm0
        vmovdqu   xmmword ptr [rsp + 8], xmm0
        vmovdqu   xmmword ptr [rsp + 24], xmm0
        lea       r11, [rsp + 8]
        lea       r9, c_pow2                 ; indexing a .const symbol directly is LNK2017
        xor       r8d, r8d
pb_bld:
        ; `bts dword ptr [r11], eax` expresses this in ONE instruction and was the first cut, but a
        ; bit-test-and-set with a REGISTER bit offset and a memory operand is microcoded; it is a
        ; read-modify-write whose address depends on the offset, and the set-13 class paid for it.
        ; Splitting it into an explicit byte index and a table-driven bit does the same work in
        ; simple ops.
        movzx     eax, byte ptr [rdx + r8]
        test      al, al
        jz        pb_built
        mov       r10d, eax                  ; NOT ecx: rcx still holds the subject pointer, and
        shr       r10d, 3                    ;   clobbering it here cost one silent crash
        and       eax, 7
        movzx     eax, byte ptr [r9 + rax]   ; 1 << (b & 7)
        or        byte ptr [r11 + r10], al
        inc       r8d
        jmp       pb_bld
pb_built:
        ; Two 16-byte broadcasts, deliberately, even though both forward from the narrower byte-wide writes
        ; just made. Reading all 32 bytes once and splitting the halves with vperm2i128 pays that
        ; forwarding stall only once, and was tried: it measured WORSE (geomean 125.54 -> 120.19),
        ; because vperm2i128 crosses lanes and two of them cost more than the stall they remove.
        vbroadcasti128 ymm1, xmmword ptr [rsp + 8]
        vbroadcasti128 ymm2, xmmword ptr [rsp + 24]
        vbroadcasti128 ymm3, xmmword ptr [c_pow2]

        mov       r11, rcx                   ; subject base
        mov       r9, rcx
        and       r9, -32                    ; align DOWN: never touches an earlier page
        mov       ecx, r11d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        CLASSIFY
        shrx      eax, eax, ecx              ; bit b now means byte b of the subject
        xor       r10d, r10d                 ; byte offset of bit 0 of the mask
        test      eax, eax
        jnz       pb_hit
        mov       r10d, 32
        sub       r10d, ecx                  ; bytes the first block covered

ALIGN 16
pb_loop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        CLASSIFY
        test      eax, eax
        jnz       pb_hit
        add       r10d, 32
        jmp       pb_loop

pb_hit:
        tzcnt     eax, eax
        add       eax, r10d                  ; byte offset of the stop
        ; The stop is a set member or the terminator, and a NUL can never be a member, so the byte
        ; itself decides which.
        cmp       byte ptr [r11 + rax], 0
        je        pb_none
        add       rax, r11
        vzeroupper
        ret
pb_none:
        xor       eax, eax
        vzeroupper
        ret

pb_zero:
        xor       eax, eax
        ret
wia_strpbrka ENDP
END
