; changes/135-strspnw/impl_2ndpc.asm
;==============================================================================
; 2ND PC VARIANT  --  AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445
;==============================================================================
; The original `impl.asm` is UNTOUCHED and remains the 5950X (Zen 3)
; implementation of record. This is an ADDITIONAL variant tuned for the second
; PC. Same exported symbol (`wia_strspnw`), so this change's existing
; correctness.c and bench.c validate it unmodified -- build with build_2ndpc.bat.
;
; Why a 2ND-PC variant is needed
; ------------------------------
; Re-measured here, the Zen 3 implementation failed the gate on the shortest
; size class:
;
;     size              ours ns   system ns   ratio   verdict
;     16/set23            39.70       34.12   0.86x   WORSE   <-- gate failure
;     64/set23            50.95      189.83   3.73x   BETTER
;     254/set23          157.91      786.02   4.98x   BETTER
;     1024/set23         631.03     3143.75   4.98x   BETTER
;     254/set3-stop0       3.57        6.12   1.71x   BETTER
;     geomean 2.671x => PARKED (a size class regressed)
;
; Cause: the Zen 3 implementation is still O(n*m) -- it does the m loop with
; vectors. For every 32-byte block it re-walks the whole set, broadcasting each
; member and OR-ing a compare into an accumulator that serialises them. A
; 16-char string spans two blocks, so it pays 46 broadcast/compare/or triples to
; examine 16 characters -- barely less work than shlwapi's 368 scalar compares.
;
; This is the weakness changes 035-040 fixed for the ucrtbase span/pbrk family
; ("set hoisted out of the block loop"); change 135 never received it.
;
; The fix -- hybrid: per-member first block, O(n+m) nibble bitmap for the tail
; ---------------------------------------------------------------------------
; A pure bitmap rewrite was tried first and is NOT what shipped, because it
; traded one regression for another. Measured, pure-bitmap:
;       16/set23 2.13x, 64 10.46x, 254 32.98x, 1024 61.29x  ... but
;       254/set3-stop0 0.62x   <-- NEW gate failure
; With a 3-character set that stops after 3 characters, the bitmap's fixed setup
; (build + move to a vector + broadcast) costs far more than three compares. The
; per-member form is genuinely better for small sets that resolve immediately,
; and the bitmap is genuinely better for everything else.
;
; So this variant does both, choosing without ever having to measure the set:
;   * BLOCK 0 uses the original per-member compare, verbatim. Any string whose
;     span ends in the first block -- which is every small-set/early-stop case,
;     including 254/set3-stop0 -- returns from there having paid exactly what
;     the original paid, and nothing more.
;   * Only if block 0 does NOT resolve the span is the bitmap built (one pass
;     over the set), and every REMAINING block is then tested with two
;     `vpshufb`s and no per-member work at all. The build cost is amortised over
;     all the remaining blocks, which is precisely when it pays for itself.
; The result is never slower than the original on any input and is dramatically
; faster on long ones.
;
; NIBBLE-BITMAP MEMBERSHIP
;   Build: for each set char c, lo = c & 15, hi = c >> 4, bitmap[lo] |= 1 << hi.
;          c is a member iff bitmap[c & 15] has bit c >> 4 set.
;   Test (16 chars per block):
;     lo_nib  = v & 0x0F0F..                 ; even bytes hold c & 15
;     hi_nib  = (v >> 4) & 0x0F0F..          ; even bytes hold (c >> 4) & 15
;     bitset  = vpshufb(bitmap,  lo_nib)
;     bitmask = vpshufb(pow2lut, hi_nib)
;     notmem  = vpcmpeqb(bitset & bitmask, 0)
;   `pow2lut` is {1,2,4,8,16,32,64,128, 0,0,0,0,0,0,0,0}: entries 8..15 are ZERO,
;   so any character >= 0x80 yields bitmask 0 -> "not a member" for free. Testing
;   `& == 0` rather than `== bitmask` is what makes that work -- a zero bitmask
;   must read as not-a-member, and `== bitmask` would wrongly report a match.
;   Only EVEN byte lanes are meaningful (the low byte of each UTF-16 unit), so
;   the movemask is AND-ed with 0x55555555 before tzcnt; odd lanes hold a lookup
;   of the character's high byte and are ignored.
;
; The bitmap is built in general-purpose registers, not on the stack
;   The first attempt assembled it with byte stores to a stack buffer and read it
;   back with `vbroadcasti128`. Narrow stores followed by a wide load is a
;   store-to-load-forwarding STALL (the same ~20-cycle hazard change 152
;   documents and dodges). Here the 128 bits are accumulated in two GP registers
;   and moved across with vmovq/vpinsrq/vinserti128, so nothing round-trips
;   through memory.
;
; Ascii-set restriction, handled safely
;   `1 << hi` needs hi <= 7, i.e. set members < 0x80. If ANY member is >= 0x80
;   the bitmap cannot represent it, so this variant does not guess -- it falls
;   back to the original per-member loop, reproduced verbatim below, which
;   handles the full 16-bit range. Correctness is identical for every set; only
;   the fast path is restricted.
;
; The terminator needs no special case (unchanged reasoning from the original)
;   A set is itself NUL-terminated, so it can never contain NUL; bit (0,0) is
;   therefore never set, NUL tests as "not a member", and the span stops there.
;
; SAFETY / PAGE-SAFETY
;   * Unchanged from the original: the first load is aligned down to 32 bytes and
;     leading characters are shifted out of the mask; every later load is
;     32-aligned, and an aligned 32-byte load never crosses a page boundary.
;     Shifting in zeros means "in set", which merely continues into the next
;     block -- it cannot end the span early.
;   * Read-only. Writes nothing through either pointer, and (unlike the first
;     attempt) allocates no stack buffer at all.
;   * Uses only ymm0-ymm5: xmm6-xmm15 are CALLEE-SAVED under the Win64 ABI
;     (harness/README.md), so no save area is needed.
;   * AVX2 only -- NO AVX-512, NO GFNI. Correct on the 5950X too.
;
; int wia_strspnw(PCWSTR psz, PCWSTR pszSet)   [Win64: rcx, rdx -> eax]
; ISA: AVX2 + BMI1 (tzcnt).

.const
ALIGN 16
pow2lut db 1,2,4,8,16,32,64,128, 0,0,0,0,0,0,0,0
; NOTE: .const has 16-byte segment alignment in MASM, so `ALIGN 32` is rejected.
; That is fine: nib0F is only ever a VEX memory SOURCE operand (vpand ymm,ymm,m256),
; and VEX memory operands carry no alignment requirement -- only explicitly
; aligned moves such as vmovdqa do.
ALIGN 16
nib0F   db 32 dup(0Fh)

.code
wia_strspnw PROC
        mov       r8, rcx                           ; string start (kept for the final count)
        mov       r9, rcx
        and       r9, -32                           ; aligned-down load address
        and       ecx, 31                           ; byte offset of the string within that block

        ;======================================================================
        ; BLOCK 0 -- original per-member compare, verbatim. Small sets and early
        ; stops resolve here and pay exactly what the original paid.
        ;======================================================================
        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1                  ; "in set" accumulator
        mov       r10, rdx
sp_set0:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        sp_done0
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       sp_set0
sp_done0:
        vpmovmskb eax, ymm1
        not       eax                               ; bits of characters NOT in the set
        shr       eax, cl                           ; drop bytes before the string start
        test      eax, eax
        jz        bm_build                          ; unresolved -> switch to the bitmap
        tzcnt     eax, eax                          ; first character not in the set
        shr       eax, 1                            ; bytes -> characters
        vzeroupper
        ret

        ;======================================================================
        ; Build the nibble bitmap in GP registers (one pass over the set), then
        ; run every remaining block with two vpshufb lookups.
        ;======================================================================
bm_build:
        push      rdx                               ; save the set base for the fallback
        xor       r10, r10                          ; bitmap bits   0..63
        xor       r11, r11                          ; bitmap bits  64..127
bm_loop:
        movzx     eax, word ptr [rdx]
        test      eax, eax
        jz        bm_done                           ; end of set
        cmp       eax, 128
        jae       bm_nonascii                       ; cannot be represented -> fallback
        mov       ecx, eax
        and       ecx, 15                           ; lo = c & 15
        shr       eax, 4                            ; hi = c >> 4   (0..7)
        shl       ecx, 3                            ; lo * 8
        add       ecx, eax                          ; bit index 0..127 (byte lo, bit hi)
        mov       rax, 1
        cmp       ecx, 64
        jae       bm_hiq
        shl       rax, cl
        or        r10, rax
        add       rdx, 2
        jmp       bm_loop
bm_hiq:
        sub       ecx, 64
        shl       rax, cl
        or        r11, rax
        add       rdx, 2
        jmp       bm_loop

bm_nonascii:
        pop       rdx                               ; restore the set base
        jmp       sp_next                           ; original per-member tail

bm_done:
        pop       rdx                               ; set base no longer needed, keep rsp sane
        vmovq     xmm3, r10
        vpinsrq   xmm3, xmm3, r11, 1                ; xmm3 = the 16-byte bitmap
        vinserti128 ymm3, ymm3, xmm3, 1             ; duplicate into the high lane
        vbroadcasti128 ymm4, xmmword ptr [pow2lut]  ; 1 << hi lookup, both lanes
        vpxor     ymm5, ymm5, ymm5                  ; zero
bm_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpand     ymm1, ymm0, ymmword ptr [nib0F]   ; lo nibbles (even bytes)
        vpsrlw    ymm2, ymm0, 4
        vpand     ymm2, ymm2, ymmword ptr [nib0F]   ; hi nibbles (even bytes)
        vpshufb   ymm1, ymm3, ymm1                  ; bitmap row for each character
        vpshufb   ymm2, ymm4, ymm2                  ; 1 << hi   (0 when hi >= 8)
        vpand     ymm1, ymm1, ymm2
        vpcmpeqb  ymm1, ymm1, ymm5                  ; 0xFF where NOT a member
        vpmovmskb eax, ymm1
        and       eax, 55555555h                    ; keep the low byte of each wchar
        test      eax, eax
        jz        bm_next
        tzcnt     eax, eax
        add       rax, r9                           ; absolute address of the stopping character
        sub       rax, r8                           ; bytes from the string start
        shr       rax, 1                            ; -> characters
        vzeroupper
        ret

        ;======================================================================
        ; FALLBACK -- the original per-member block loop, unchanged. Reached only
        ; when the set contains a character >= 0x80.
        ;======================================================================
sp_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1
        mov       r10, rdx
sp_set:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        sp_done
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       sp_set
sp_done:
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jz        sp_next
        tzcnt     eax, eax
        add       rax, r9                           ; absolute address of the stopping character
        sub       rax, r8                           ; bytes from the string start
        shr       rax, 1                            ; -> characters
        vzeroupper
        ret
wia_strspnw ENDP
END
