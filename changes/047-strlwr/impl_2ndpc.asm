; changes/047-strlwr/impl_2ndpc.asm
;==============================================================================
; 2ND PC VARIANT  --  AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445
;==============================================================================
; The original `impl.asm` is UNTOUCHED and remains the 5950X (Zen 3)
; implementation of record. This is an ADDITIONAL variant tuned for the second
; PC. Same exported symbol (`wia_strlwr`), so this change's existing
; correctness.c and bench.c validate it unmodified -- build with build_2ndpc.bat.
;
; Why a 2ND-PC variant is needed
; ------------------------------
; Re-measured here, the Zen 3 implementation failed the gate on one size class:
;
;     size     ours ns   system ns   ratio   verdict
;     8           5.70        4.74   0.83x   WORSE    <-- gate failure
;     32          2.99       14.36   4.80x   BETTER
;     ...       (128..32000 run 12.60x .. 25.22x)
;     geomean 9.277x => PARKED (a size class regressed)
;
; Note that an 8-byte string (5.70 ns) costs MORE than a 32-byte one (2.99 ns).
; That is not a Zen-4 vector weakness -- it is a control-path pathology. For an
; 8-char string the Zen 3 code runs THREE serialized
; `load -> vpcmpeqb -> vpmovmskb -> test -> branch` chains (the 32-byte probe,
; the 8-byte probe, then the 32-byte probe AGAIN because `try8` jumps back to
; `loop0`, then the 8-byte probe again). Each such chain is ~9 cycles of pure
; latency on Zen 4, so ~27 cycles go on *deciding* how to fold eight bytes.
;
; A first attempt replaced the short path with a scalar byte loop. That was
; WORSE (0.69x): a branch-per-byte fold runs ~2.5 cycles/byte, so nine bytes
; cost more than the vector setup it removed. Recorded here because it is the
; obvious fix and it does not work.
;
; The fix that works
; ------------------
; Handle short strings entirely in GENERAL-PURPOSE registers -- no vector unit,
; therefore none of those movemask->branch chains, and 8 bytes per operation
; instead of one:
;
;   1. Has-zero probe (the classic (v - 0x01..01) & ~v & 0x80..80) on the first
;      two 8-byte words. Its dependency chain is ~8 cycles and the two words are
;      independent, so both probes overlap.
;   2. If the terminator lies in bytes 8..15, fold bytes 0..7 with a SWAR case
;      fold (below), store all eight at once, then finish the last <= 8 bytes
;      with the scalar loop -- for an 8-char string that is exactly one
;      iteration, because byte 8 IS the terminator.
;   3. If the terminator is in bytes 0..7, go straight to the scalar loop
;      (at most 7 folds).
;   4. If there is no terminator in the first 16 bytes, fall into the original
;      vector body entered at offset 0, completely unchanged -- no duplicated
;      work, and the only added cost on long strings is the ~12-instruction
;      probe, against 4.80x-25.22x of existing headroom.
;
; SWAR CASE FOLD (branch-free, 8 bytes at a time)
;     ge = (v + 0x3F..3F) & 0x80..80    ; byte high bit set iff byte >= 0x41 'A'
;     gt = (v + 0x25..25) & 0x80..80    ; byte high bit set iff byte >= 0x5B '['
;     m  = (ge & ~gt) >> 2              ; 0x20 exactly in the A..Z byte lanes
;     v += m
;   Verified at the boundaries: 'A'(41)->ge set, gt clear -> folds. 'Z'(5A)->
;   ge set (0x99), gt clear (0x7F) -> folds. '['(5B)-> both set -> no fold.
;   '@'(40)-> ge clear -> no fold. 0x00 -> no fold. The `>> 2` cannot leak
;   across lanes because 0x80 >> 2 = 0x20 stays inside its own byte.
;
;   CARRY SAFETY: the adds would carry between bytes if any byte were >= 0xC1,
;   so the fold is guarded by `test v, 0x80..80` -- if ANY byte has its high bit
;   set (non-ASCII), the SWAR path is abandoned and the scalar loop runs
;   instead. For bytes < 0x80, v + 0x3F <= 0xBE and v + 0x25 <= 0xA4, so no
;   byte addition can ever carry into its neighbour.
;
; Contract preserved exactly
;   * In the default C locale ucrtbase folds only ASCII A-Z -> a-z (verified by
;     this change's correctness.c against the live export). All three paths --
;     SWAR, scalar and the untouched vector body -- implement that same map.
;   * Returns the original pointer `s` in rax.
;   * In place. Never writes at or past the terminator: the SWAR store covers
;     bytes 0..7 only on the branch that has PROVEN the terminator is at index
;     >= 8, and folding leaves every non-A-Z byte bit-identical anyway.
;
; SAFETY / PAGE-SAFETY
;   * The two 8-byte probe loads happen only when (s & 4095) <= 4080, which
;     guarantees bytes [s, s+16) are in the same 4 KB page as s -- a page that
;     must be mapped, since the string starts in it. Nearer than 16 bytes to a
;     page end the probe is skipped and the original (already page-safe) path
;     runs. This variant can therefore never fault where the original would not.
;   * Plain scalar x86-64 in the added code: NO AVX-512, NO GFNI. It is
;     correct on the 5950X too -- just unnecessary there.
;   * Clobbers only volatile registers (rdx, r8, r9, r10, r11) per the Win64 ABI.
;
; char* wia_strlwr(char* s)   [Win64: rcx -> rax (returns s)]

; Only ymm0-ymm5 may be used. xmm6-xmm15 are callee-saved under Win64 -- their low 128 Bits are,
; the upper halves are volatile -- so an earlier cut of this function, which parked its fold
; constants in ymm6/ymm7, silently destroyed any double the caller had live. That is invisible to a
; correctness test, which compares integers, and invisible to a benchmark unless the benchmark
; happens to keep its accumulators there. See tools/abi-check.
;
; Fitting in six registers costs nothing here. The range test was
;       (c > LO) AND (HI+1 > c)
; whose second compare wants the constant as the FIRST operand, so it had to live in a register.
; Rewritten as
;       (c > Lo) and not (c > hi)
; both compares take c first, so the bound becomes an ordinary register compare and the two ANDs
; collapse into one vpandn; the fold delta and the zero vector become memory operands. Identical
; instruction count, three fewer live registers.
.const
c40b db 40h
c5Ab db 5Ah
; 32-byte forms of the constants now used as memory operands. VEX operands carry no
; alignment requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m db 32 dup(20h)
zerom db 32 dup(0)
.code
wia_strlwr PROC
        mov       rax, rcx                          ; return value = s
        mov       r8, rcx                           ; cursor

        ;----------------------------------------------------------------------
        ; 2ND PC short-string path (general-purpose registers only)
        ;----------------------------------------------------------------------
        mov       r9d, ecx
        and       r9d, 4095
        cmp       r9d, 4080                         ; 16 bytes must lie inside this page
        ja        vec_setup                         ; too near page end -> original path

        ; ---- has-zero on bytes 0..7 ----
        mov       r10, qword ptr [rcx]
        mov       r11, r10
        not       r11                               ; ~v0
        mov       rdx, 0101010101010101h
        sub       r10, rdx                          ; v0 - 0x01..01
        and       r10, r11
        mov       r11, 8080808080808080h
        and       r10, r11                          ; has-zero(v0)
        jnz       short_scalar                      ; terminator in 0..7 -> scalar, <=7 folds

        ; ---- has-zero on bytes 8..15 ----
        mov       r10, qword ptr [rcx + 8]
        mov       r9, r10
        not       r9                                ; ~v1
        sub       r10, rdx                          ; v1 - 0x01..01
        and       r10, r9
        and       r10, r11                          ; has-zero(v1)
        jz        vec_setup                         ; longer than 16 B -> original path

        ; ---- terminator is in bytes 8..15: SWAR-fold bytes 0..7 ----
        mov       r9, qword ptr [rcx]               ; v0 again (L1 hit)
        test      r9, r11                           ; any byte >= 0x80 (non-ASCII)?
        jnz       short_scalar                      ; yes -> scalar fallback, no carry risk
        mov       rdx, 3F3F3F3F3F3F3F3Fh
        lea       r10, [r9 + rdx]                   ; v + 0x3F..3F
        and       r10, r11                          ; ge = >= 'A'
        mov       rdx, 2525252525252525h
        add       rdx, r9                           ; v + 0x25..25
        and       rdx, r11                          ; gt = >= '['
        not       rdx
        and       r10, rdx                          ; ge & ~gt  -> in 'A'..'Z'
        shr       r10, 2                            ; -> 0x20 per folded lane
        add       r9, r10
        mov       qword ptr [rcx], r9               ; store all eight folded bytes
        add       r8, 8                             ; finish the tail scalar (<= 8 bytes)

        ;----------------------------------------------------------------------
        ; Scalar tail / fallback. Identical fold rule to `step1`, but no YMM
        ; register is ever touched on this path, so it needs no vzeroupper and
        ; pays no vector-constant setup.
        ;----------------------------------------------------------------------
short_scalar:
        movzx     r9d, byte ptr [r8]
        test      r9b, r9b
        jz        short_done
        lea       r10d, [r9d - 41h]                 ; b - 'A'
        cmp       r10d, 19h                         ; unsigned <= 25 => 'A'..'Z'
        ja        short_nofold
        add       r9d, 20h
        mov       byte ptr [r8], r9b
short_nofold:
        inc       r8
        jmp       short_scalar
short_done:
        ret

        ;----------------------------------------------------------------------
        ; ORIGINAL BODY -- unchanged from impl.asm, entered at offset 0.
        ;----------------------------------------------------------------------
vec_setup:
        vpbroadcastb ymm1, byte ptr [c40b]          ; low bound
        vpbroadcastb ymm2, byte ptr [c5Ab]          ; high bound, now inclusive

loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                          ; within 32 bytes of page end?
        ja        try8
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqb  ymm3, ymm0, ymmword ptr [zerom]   ; terminator
        vpmovmskb r9d, ymm3
        test      r9d, r9d
        jnz       try8                              ; terminator in this 32 -> try smaller
        vpcmpgtb  ymm3, ymm0, ymm1                  ; c > LO
        vpcmpgtb  ymm4, ymm0, ymm2                  ; c > HI
        vpandn    ymm3, ymm4, ymm3                  ; (c > LO) AND NOT (c > HI)
        vpand     ymm3, ymm3, ymmword ptr [c20m]
        vpaddb    ymm0, ymm0, ymm3
        vmovdqu   ymmword ptr [r8], ymm0
        add       r8, 32
        jmp       loop0

try8:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4088                          ; within 8 bytes of page end?
        ja        step1
        vmovq     xmm0, qword ptr [r8]
        vpcmpeqb  xmm3, xmm0, xmmword ptr [zerom]   ; terminator
        vpmovmskb r9d, xmm3
        and       r9d, 0FFh
        test      r9d, r9d
        jnz       step1                             ; terminator in this 8 -> scalar
        vpcmpgtb  xmm3, xmm0, xmm1                  ; c > LO
        vpcmpgtb  xmm4, xmm0, xmm2                  ; c > HI
        vpandn    xmm3, xmm4, xmm3                  ; (c > LO) AND NOT (c > HI)
        vpand     xmm3, xmm3, xmmword ptr [c20m]
        vpaddb    xmm0, xmm0, xmm3
        vmovq     qword ptr [r8], xmm0
        add       r8, 8
        jmp       loop0

step1:
        movzx     r9d, byte ptr [r8]
        test      r9b, r9b
        jz        done
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        nofold
        add       r9d, 20h
        mov       byte ptr [r8], r9b
nofold:
        inc       r8
        jmp       step1

done:
        vzeroupper
        ret
wia_strlwr ENDP
END
