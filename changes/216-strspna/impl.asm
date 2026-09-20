; changes/216-strspna/impl.asm
; int wia_strspna(PCSTR pszStr, PCSTR pszSet)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!StrSpnA: the number of leading characters of pszStr that ARE in pszSet.
;
; This is the slowest single routine the narrow survey measured anywhere: 166503.08 ns to span 4000
; characters (166 MICROSECONDS) against 22141.23 ns for StrSpnW over the same character count.
; That is 7.52x the wide cost for HALF the bytes, and the wide form was itself slow enough to be
; worth converting (change 135). An MBCS walk with a per-character search of the set is quadratic in
; the set size on top of everything else.
;
; This is change 214's core with the membership test INVERTED, and the three were written together:
; the set bitmap, the two-table vpshufb test and the page-safe aligned scan are identical.
;
; ---- what the probe settled (probes/span.c) ---------------------------------------------------------
;   * BYTE-WISE, and so are its two siblings. Every byte value 0x01..0xFF was placed where a lead byte
;     would swallow the character after it: 0 of 254 misbehave for StrCSpnA, StrPBrkA and StrSpnA
;     alike. The SET string is byte-wise too (0 of 252 values cannot be a member) so any byte can
;     belong to the set and a 256-bit membership test reproduces all of it exactly.
;   * a NULL set is not the empty set. StrCSpnA("abc", NULL) is 0, while StrCSpnA("abc", "") is 3.
;     A reimplementation that treated NULL as "no members" would return 3 and be wrong.
;   * NULL subject -> 0. Empty subject -> 0. Duplicates in the set are harmless. No length cap.
;
; ---- the observation that makes this cheap ----------------------------------------------------------
; The set string is NUL-TERMINATED, so the set can never contain a NUL, so the subject's own
; terminator is never a member, which means it is a NON-member, which means the inverted mask
; Stops there on its own. StrSpnA is therefore exactly:
;
;     the index of the first NON-member
;
; with no terminator test anywhere in the loop at all. Its sibling StrCSpnA needs an explicit NUL
; compare ORed into the mask; this one gets the same stop for free, so its inner loop is two
; instructions SHORTER than 214's despite computing the same thing.
;
; The `not` that inverts the mask is also what makes the aligned first load safe in this direction.
; Bits shifted in at the top of the mask are 0, which after inversion reads as "member", i.e. "no
; stop here", so the scan simply moves on to the next block and re-examines those bytes properly,
; exactly as it does for 214 where 0 means "no stop" directly.
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
; 1 << i for i = 0..7; indices 8..15 are never used, because (v & 7) <= 7
c_pow2  db 001h,002h,004h,008h,010h,020h,040h,080h, 0,0,0,0,0,0,0,0

.code

; Build, in eax, the mask of positions in ymm0 that are NOT a set member. The terminator is never a
; member, so it is included automatically and needs no compare of its own.
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
        vpmovmskb eax, ymm4
        not       eax                                  ; NON-member; the NUL is one of these
ENDM

wia_strspna PROC
        test      rcx, rcx
        jz        sp_zero
        test      rdx, rdx
        jz        sp_zero                    ; measured: either NULL argument returns 0

        ; ---- the set becomes a 256-bit bitmap in the caller's shadow space ----
        vpxor     xmm0, xmm0, xmm0
        vmovdqu   xmmword ptr [rsp + 8], xmm0
        vmovdqu   xmmword ptr [rsp + 24], xmm0
        lea       r11, [rsp + 8]
        lea       r9, c_pow2                 ; indexing a .const symbol directly is LNK2017
        xor       r8d, r8d
sp_bld:
        ; `bts dword ptr [r11], eax` expresses this in ONE instruction and was the first cut, but a
        ; bit-test-and-set with a REGISTER bit offset and a memory operand is microcoded; it is a
        ; read-modify-write whose address depends on the offset, and the set-13 class paid for it.
        ; Splitting it into an explicit byte index and a table-driven bit does the same work in
        ; simple ops.
        movzx     eax, byte ptr [rdx + r8]
        test      al, al
        jz        sp_built
        mov       r10d, eax                  ; NOT ecx: rcx still holds the subject pointer, and
        shr       r10d, 3                    ;   clobbering it here cost one silent crash
        and       eax, 7
        movzx     eax, byte ptr [r9 + rax]   ; 1 << (b & 7)
        or        byte ptr [r11 + r10], al
        inc       r8d
        jmp       sp_bld
sp_built:
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
        jnz       sp_hit
        mov       r10d, 32
        sub       r10d, ecx                  ; bytes the first block covered

ALIGN 16
sp_loop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        CLASSIFY
        test      eax, eax
        jnz       sp_hit
        add       r10d, 32
        jmp       sp_loop

sp_hit:
        tzcnt     eax, eax
        add       eax, r10d                  ; the first non-member: the span length
        vzeroupper
        ret

sp_zero:
        xor       eax, eax
        ret
wia_strspna ENDP
END
