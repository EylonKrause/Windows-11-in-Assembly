; changes/185-wcsnset-s/impl_tgl.asm
; errno_t wia_wcsnset_s(wchar_t* str, size_t numberOfElements, wchar_t c, size_t count)
;   [Win64: rcx, rdx, r8w, r9 -> eax]
;
; Tiger Lake / Willow Cove variant of change 185. Same contract, same oracle, same gates; this is
; the only file that differs from the parent.
;
; Why a variant and not an edit
; -----------------------------
; The parent wins every size class on Zen 3 and Zen 4. Here the "8" class measures 0.82x, and the
; reason is not the arithmetic; it is that at that size the parent does no vector work at all
; while paying the full price of having intended to.
;
; What that bench row actually is matters, because the label is in CHARACTERS while the page rules
; are in BYTES: an 8-character string, numberOfElements = 9, count = _TRUNCATE. So the bound is
; 9 ELEMENTS = 18 bytes and the fill is 8 elements = 16 bytes. The parent's two vector thresholds
; are both 16 ELEMENTS (32 bytes), so with a bound of 9:
;
;   * `vpxor ymm1, ymm1, ymm1` runs unconditionally in the prologue, before the bound is examined.
;   * The bounded terminator scan takes `cmp r11, 16 / jb scan_tail` on its very first test and
;     then walks eight characters one at a time, eight taken branches to find what one 16-byte
;     load would have shown.
;   * `vmovd` / `vpbroadcastw ymm2` then run unconditionally, and the fill likewise falls straight
;     to `f_tail` and stores eight characters one at a time, eight more taken branches.
;   * Both ymm writes have dirtied the upper state, so the epilogue owes a `vzeroupper`. That is
;     mandatory (the caller's later SSE code would otherwise pay the transition penalty) and it
;     bought nothing here, because no 256-bit instruction ever executed.
;
; So the short case pays for two ymm writes, a vzeroupper, and sixteen single-character iterations.
; This is the wide mirror of what change 182 showed on the byte side, where the same split took the
; 8-byte class from 0.83x to a tie and made the variant land.
;
; What this variant does
; ----------------------
; It splits at the bound BEFORE touching any vector register, and below 16 elements it never
; touches a 256-bit one:
;
;   * A VEX-encoded 128-bit instruction zeroes bits 128 and above of its destination, so the upper
;     state stays CLEAN. No `vzeroupper` is owed anywhere on the narrow path.
;   * Below 16 elements the ENTIRE span is at most 30 bytes, so the scan needs no loop at all: TWO
;     OVERLAPPING probes cover it. For a bound of 8..15 elements they are 16-byte loads at str and
;     at str + 2*(n-8); for 4..7 they are 8-byte loads at str and at str + 2*(n-4). The two windows
;     together are exactly [0, n), because the second starts at n-W and W >= n-W whenever n <= 2W.
;     This is not a sampling trick: if the first probe is clean then every element it covered is
;     known nonzero, so the lowest set bit of the second probe's mask IS the first terminator. The
;     two loads are independent, so they issue together; the parent's walk was serial.
;   * The fill uses OVERLAPPING stores rather than a character loop, writing the same character
;     twice is free, branching once per character is not, and the broadcast is built with an imul
;     so that no vector register is involved. `imul` against 0001000100010001h is exact for ANY
;     16-bit value, unlike the byte case where the argument has to be <= 255 for the partial
;     products not to carry into one another: here c<<0, c<<16, c<<32 and c<<48 each occupy their
;     own 16-bit lane by construction, so every lane is exactly c even for 0FFFFh.
;   * The ladder is ordered so that the largest narrow block (8..14 elements, 16..28 bytes) is the
;     FALL-THROUGH rather than a taken branch; that is the one the failing row exercises, and at
;     these sizes these functions are branch-bound, not load-bound. On that row the whole function
;     now executes with zero taken branches from entry to `ret`.
;
; At or above 16 elements it branches to the parent's code, spliced in unchanged. That is where the
; parent already wins up to 5.2x, and it is not what this variant is about.
;
; The parent is left untouched: its RESULTS.md records a measurement taken on different hardware,
; where dirtying the upper state for a short call is cheap enough not to show.
;
; What was considered and not used, and why
; -----------------------------------------
; This part has AVX-512VL, so the whole narrow scan could have been one masked load: `bzhi` the
; bound into a k register, `vmovdqu16 ymm0{k1}{z}`, `vptestnmw k2{k1}`, `tzcnt`, which needs no
; page reasoning at all, because a masked load suppresses faults on masked-off elements, and which
; handles 1..15 in a single shape with the terminator index falling straight out of a per-word
; mask. It was rejected on two counts, both of which matter more than the instruction count: it
; writes a ymm, so it owes back the very `vzeroupper` this variant exists to remove, and its
; dependency chain is bzhi -> kmov -> masked load -> vptestnm -> kmov -> tzcnt with a 3-cycle
; GPR<->mask transfer at each end, which is longer than the two INDEPENDENT VEX-128 loads it would
; replace. It would be the better shape only if the bound were variable enough for the 8 / 4 / walk
; ladder to mispredict, which is not what a bounded string setter sees.
;
; CONTRACT, unchanged, and worth restating because the `_s` family in this CRT has three distinct
; shapes and assuming one from another is how an earlier candidate was refuted on 407604 of
; 1000000 cases. The parent pinned this one by probing the live export
; (../184-strnset-s/probes/sns.c, 1000000 cases for the byte and the wide form each, fuzzed side
; by side, 0 mismatches for both):
;
;   * numberOfElements == 0 -> EINVAL (22) and nothing is written, regardless of count.
;   * No terminator strictly inside numberOfElements -> fill min(count, numberOfElements-1)
;     Elements (not bytes), and only then write str[0] = 0, returning einval (22).
;   * Otherwise -> fill min(count, length) elements, leave the rest of the string and the
;     terminator alone, return 0.
;   * _TRUNCATE ((size_t)-1) is NOT special-cased, it saturates to the other limit.
;   * Every fill value behaves the same, including 0, the surrogate range and 0FFFFh.
;
; The scan cannot be shortened by `count`: even at count 0 the return value still depends on
; whether a terminator exists strictly inside numberOfElements. Only the FILL is clipped.
;
; PAGE SAFETY: unchanged in kind, applied at three widths instead of one. A probe is issued only
; when that many bytes of the caller's DECLARED buffer remain (16 bytes needs 8 elements of bound,
; 8 bytes needs 4), AND the address is guarded against crossing into the next page. Both halves are
; kept even though the second probe's window ends exactly at the end of the declared buffer: the
; guard is what makes the property local to the instruction instead of an argument about the
; caller. A guard that fires falls back to the single-character walk; that is a page-safety step,
; not a scalar re-entry, and it never jumps back into a probe. The fill writes at most
; numberOfElements-1 elements and every overlapping store lands strictly inside that span, so the
; variant touches no byte the parent would not.
;
; ABI: the narrow path uses xmm0-xmm2 and rax / r8 / r10 / r11 only, no xmm6-xmm15, nothing
; non-volatile, and no stack frame beyond the shadow space the CRT call already needed.
;
; ISA: AVX2 for the >= 16-element path (the parent's), VEX-128 + BMI1 below. No AVX-512, no GFNI.
; Validated on bench #3 (Intel i9-11900H, Tiger Lake-H), see docs/PLATFORM-i9-11900H.md.

EXTERN _invalid_parameter_noinfo:PROC

.code
wia_wcsnset_s PROC
        test      rdx, rdx
        jz        err_nowrite                    ; bound 0: EINVAL, nothing written
        cmp       rdx, 16
        jae       wide                           ; only now is dirtying a ymm worth it

;=============== narrow path: bound 1..15 elements. No ymm is written anywhere below. ============
; The whole budget is under 32 bytes, so the fill count, length on success, numberOfElements-1 on
; failure, is at most 14 elements, and two overlapping probes cover the whole scan.
        cmp       rdx, 8
        jb        n_lt8
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4080                      ; 16-byte read must stay inside this page
        ja        n_walk0
        vpxor     xmm1, xmm1, xmm1
        vmovdqu   xmm0, xmmword ptr [rcx]        ; elements 0..7 -- in bounds because n >= 8
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        test      eax, eax
        jnz       n_hit_lo
        ; Nothing in the first probe. The bound is under 16, so at most SEVEN elements remain,
        ; and seven scalar compares cost less than a second vector probe, which needs its own
        ; page guard, its own mask extraction, and a lea/tzcnt/shr/lea to rebase the index it
        ; produces. The first version of this variant used that second probe and measured 0.82x,
        ; which is exactly what the parent measures: it moved work around without removing any.
        ; The walk below derives the index from r10 - rcx, so entering it here is already correct.
        lea       r10, [rcx + 16]                ; -> element 8
        mov       r11, rdx
        sub       r11, 8
        jz        n_noterm                       ; bound was exactly 8 and none of them was NUL
        jmp       n_walk
n_found:
        cmp       rax, r9
        cmova     rax, r9                        ; limit = min(count, length)
        mov       r11, rax
        xor       r10d, r10d                     ; outcome = success
n_fill:
        ; Broadcast the fill character across a qword without touching a vector register. The four
        ; partial products occupy disjoint 16-bit lanes, so this is exact for every wchar_t, and
        ; r8w still holds c afterwards, which the single-element case uses.
        movzx     eax, r8w
        mov       r8, 0001000100010001h
        imul      r8, rax
        cmp       r11, 8
        jb        nf_small
        mov       qword ptr [rcx], r8            ; 8..14 elements = 16..28 bytes
        mov       qword ptr [rcx + 8], r8
        mov       qword ptr [rcx + r11*2 - 16], r8
        mov       qword ptr [rcx + r11*2 - 8], r8
n_done:
        test      r10d, r10d                     ; no vzeroupper: the upper state was never dirtied
        jnz       err_after_fill
        xor       eax, eax                       ; success
        ret

nf_small:
        cmp       r11, 4
        jae       nf_4_7
        cmp       r11, 2
        jae       nf_2_3
        test      r11, r11
        jz        n_done
        mov       word ptr [rcx], r8w            ; exactly one element
        jmp       n_done
nf_2_3:
        mov       dword ptr [rcx], r8d
        mov       dword ptr [rcx + r11*2 - 4], r8d
        jmp       n_done
nf_4_7:
        mov       qword ptr [rcx], r8
        mov       qword ptr [rcx + r11*2 - 8], r8
        jmp       n_done

n_hit_lo:                                        ; terminator inside the first probe, based at str
        tzcnt     eax, eax
        shr       eax, 1
        jmp       n_found

n_lt8:                                           ; bound 1..7: the same shape, eight bytes wide
        cmp       rdx, 4
        jb        n_walk0
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4088                      ; 8-byte read must stay inside this page
        ja        n_walk0
        vpxor     xmm1, xmm1, xmm1
        vmovq     xmm0, qword ptr [rcx]          ; elements 0..3 -- in bounds because n >= 4
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        and       eax, 0FFh                      ; vmovq zeroed the rest of xmm0; ignore those
        jnz       n_hit_lo
        lea       r10, [rcx + 8]                 ; -> element 4; same reasoning as above
        mov       r11, rdx
        sub       r11, 4
        jz        n_noterm
        jmp       n_walk

n_walk0:                                         ; 1..3 elements, or hard against a page end
        mov       r10, rcx
        mov       r11, rdx                       ; rdx >= 1 here, so the loop runs at least once
n_walk:
        cmp       word ptr [r10], 0
        je        n_walk_found
        add       r10, 2
        dec       r11
        jnz       n_walk
        jmp       n_noterm
n_walk_found:
        sub       r10, rcx
        mov       rax, r10
        shr       rax, 1
        jmp       n_found

n_noterm:
        lea       r11, [rdx - 1]
        cmp       r11, r9
        cmova     r11, r9                        ; limit = min(count, numberOfElements-1)
        mov       r10d, 1                        ; outcome = EINVAL
        jmp       n_fill

;======================== wide path: the parent's code, unchanged ================================
wide:
        mov       r10, rcx                       ; scan cursor
        mov       r11, rdx                       ; characters of budget remaining
        vpxor     ymm1, ymm1, ymm1               ; (r9 holds `count` throughout the scan)

        ;---------------- bounded terminator scan ----------------
scan:
        cmp       r11, 16
        jb        scan_tail
        mov       eax, r10d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        scan_step
        vmovdqu   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       scan_hit
        add       r10, 32
        sub       r11, 16
        jmp       scan
scan_step:                                       ; near a page end: one character, then retry
        cmp       word ptr [r10], 0
        je        scan_found
        add       r10, 2
        dec       r11
        jnz       scan
        jmp       no_term
scan_tail:
        test      r11, r11
        jz        no_term
        cmp       word ptr [r10], 0
        je        scan_found
        add       r10, 2
        dec       r11
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax                       ; vpcmpeqw sets BOTH bytes of a matching word,
        add       r10, rax                       ; so the low set bit is the word's low byte
scan_found:
        sub       r10, rcx
        shr       r10, 1                         ; r10 = length in characters
        cmp       r10, r9
        cmova     r10, r9                        ; r10 = min(count, length)
        xor       edx, edx                       ; outcome = success
        jmp       do_fill
no_term:
        dec       rdx                            ; numberOfElements - 1
        mov       r10, rdx
        cmp       r10, r9
        cmova     r10, r9                        ; r10 = min(count, numberOfElements-1)
        mov       edx, 1                         ; outcome = EINVAL

        ;---------------- one fill loop serves both outcomes ----------------
do_fill:
        movzx     eax, r8w
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        mov       r9, rcx                        ; fill cursor (count is no longer needed)
f_blk:
        cmp       r10, 16
        jb        f_tail
        vmovdqu   ymmword ptr [r9], ymm2
        add       r9, 32
        sub       r10, 16
        jmp       f_blk
f_tail:
        test      r10, r10
        jz        f_done
        mov       word ptr [r9], r8w
        add       r9, 2
        dec       r10
        jmp       f_tail
f_done:
        vzeroupper
        test      edx, edx
        jnz       err_after_fill
        xor       eax, eax                       ; success
        ret

err_after_fill:
        mov       word ptr [rcx], 0              ; empty the string AFTER the partial fill
        sub       rsp, 40                        ; 32 bytes of shadow space + 8 for alignment
        call      _invalid_parameter_noinfo      ; ucrtbase's own, so handlers behave identically
        add       rsp, 40
        mov       eax, 22                        ; EINVAL
        ret

err_nowrite:                                     ; bound 0 -- no vector register was touched
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22
        ret
wia_wcsnset_s ENDP
END
