; shlwapi.dll!StrCSpnA  --  hand-written x86-64 reimplementation (137.08x vs shipped)
; source of truth: changes/214-strcspna/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/214-strcspna/impl.asm
; int wia_strcspna(PCSTR pszStr, PCSTR pszSet)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!StrCSpnA: the number of leading characters of pszStr that are NOT in pszSet.
; The live export costs 42868.10 ns on 4000 characters against 3166.56 ns for StrCSpnW over the same
; character count, 13.54x the wide cost for HALF the bytes, the MBCS-walk signature this project
; has now seen across the whole narrow shlwapi family.
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
; terminator is never a member. StrCSpnA is therefore exactly:
;
;     the index of the first position that is EITHER a set member OR the terminator
;
; one scan, one mask, no separate length pass and no second stopping rule. (Its sibling StrSpnA
; gets the same gift from the other side: the terminator is never a member, so "first non-member"
; already stops there.)
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

wia_strcspna PROC
        test      rcx, rcx
        jz        cs_zero
        test      rdx, rdx
        jz        cs_zero                    ; measured: a NULL set returns 0, NOT strlen

        ; ---- the set becomes a 256-bit bitmap in the caller's shadow space ----
        vpxor     xmm0, xmm0, xmm0
        vmovdqu   xmmword ptr [rsp + 8], xmm0
        vmovdqu   xmmword ptr [rsp + 24], xmm0
        lea       r11, [rsp + 8]
        lea       r9, c_pow2                 ; indexing a .const symbol directly is LNK2017
        xor       r8d, r8d
cs_bld:
        ; `bts dword ptr [r11], eax` expresses this in ONE instruction and was the first cut, but a
        ; bit-test-and-set with a REGISTER bit offset and a memory operand is microcoded; it is a
        ; read-modify-write whose address depends on the offset, and the set-13 class paid for it.
        ; Splitting it into an explicit byte index and a table-driven bit does the same work in
        ; simple ops.
        movzx     eax, byte ptr [rdx + r8]
        test      al, al
        jz        cs_built
        mov       r10d, eax                  ; NOT ecx: rcx still holds the subject pointer, and
        shr       r10d, 3                    ;   clobbering it here cost one silent crash
        and       eax, 7
        movzx     eax, byte ptr [r9 + rax]   ; 1 << (b & 7)
        or        byte ptr [r11 + r10], al
        inc       r8d
        jmp       cs_bld
cs_built:
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
        jnz       cs_hit
        mov       r10d, 32
        sub       r10d, ecx                  ; bytes the first block covered

ALIGN 16
cs_loop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        CLASSIFY
        test      eax, eax
        jnz       cs_hit
        add       r10d, 32
        jmp       cs_loop

cs_hit:
        tzcnt     eax, eax
        add       eax, r10d                  ; a member or the terminator: either way, the answer
        vzeroupper
        ret

cs_zero:
        xor       eax, eax
        ret
wia_strcspna ENDP
END
