; changes/183-wcsset-s/impl_tgl.asm
; errno_t wia_wcsset_s(wchar_t* str, size_t numberOfElements, wchar_t c)
;   [Win64: rcx, rdx, r8w -> eax]
;
; Tiger Lake / Willow Cove variant of change 183. Same contract, same oracle, same gates; this is
; the only file that differs from the parent, and the parent's wide path is spliced in below byte
; for byte rather than retyped.
;
; Why a variant and not an edit
; -----------------------------
; The parent wins every size class on Zen 3 and Zen 4. Here it measures 0.78x at the smallest
; class, and the reason is not the arithmetic; it is that at that size the parent does NO VECTOR
; Work at all while paying the full price of having intended to.
;
; What the bench's "8" row actually is matters, and it is not eight bytes: bench.c builds an
; 8-CHARACTER string and calls with numberOfElements = 9, so the call scans 9 wchar_t = 18 bytes
; and fills 8 wchar_t = 16 bytes. The parent's vector width is 16 characters, so at that size:
;
;   * `vpxor ymm1, ymm1, ymm1` runs unconditionally in the prologue, before the bound is examined.
;   * The bounded scan takes `cmp r10, 16 / jb scan_tail` on its very first test and then walks
;     eight characters one at a time, two bytes per iteration.
;   * `vpbroadcastw ymm2` then runs unconditionally, and the fill likewise falls straight to
;     `f_tail` and stores eight characters one at a time.
;   * Both ymm writes have dirtied the upper state, so the epilogue owes a `vzeroupper`, which is
;     mandatory (the caller's later SSE code would otherwise pay a transition penalty) and bought
;     nothing here.
;
; So the short case pays for two ymm writes, a `vzeroupper` and sixteen single-character
; iterations, and gets no vector work in return. This is the same defect the sibling change 182
; showed on this machine at 0.83x; it bites harder in the wide form because a string of a given
; character count only reaches the parent's 16-character threshold at twice the byte length, so
; MORE of the useful small-string range falls into the walking path.
;
; What the variant changes
; ------------------------
; It splits at the bound BEFORE touching any vector register. Below a 16-CHARACTER bound (32 bytes
; the parent's vector width) it stays in VEX-128 and general-purpose registers: a VEX-encoded
; 128-bit instruction zeroes bits 128 and above of its destination, so the upper state stays CLEAN
; and no `vzeroupper` is owed on that path at all. At or above that bound it branches to the
; parent's code, spliced in unchanged; that is where the parent already wins up to 3.5x, and it
; is not what this variant is about.
;
; The narrow path does not scan with a loop. A bound under 16 characters is covered entirely by two
; overlapping 16-byte probes, one at the head, one placed against the END of the declared span --
; so the whole scan is two loads, two compares and two mask extractions with no iteration at all.
; If the head mask is empty the terminator cannot lie below character 8, so wherever the two
; windows overlap the lowest set bit of the tail mask IS the first terminator; that is what makes
; the pair sufficient rather than merely convenient. The fill then uses OVERLAPPING stores rather
; than a character loop, writing the same character twice is free, branching once per character
; is not.
;
; The shape matters more than the instruction count, and that is the real lesson here. Two earlier
; versions of this narrow path were both correct and both still regressed:
;
;   * A 16-byte probe loop, then an 8-byte probe, then a one-character walk, then a fill ladder:
;     0.94x. The instruction count was already low; what was left was FIVE TAKEN branches on the
;     way through (out of the 8-character probe, out of the 4-character probe, into the walk, into
;     the fill, and picking the fill rung), and at this size the function is BRANCH-BOUND rather
;     than load- or store-bound. Change 008 on this same machine learned the identical lesson.
;   * The probe pair with the count kept in CHARACTERS: 0.96x. Every store address then needed
;     `r11*2`, and the count arrived through `tzcnt -> shr -> lea [base+index*2+disp]`, and a
;     three-component LEA is a 3-cycle single-port instruction on this core, sitting directly
;     between the scan's result and the first store address.
;
; So the count is carried in BYTES throughout. `tzcnt` on a `vpmovmskb` mask already yields a byte
; offset, the fill's rung tests are simply doubled, and every store addresses `[rcx + r10 - width]`
; with no scaling at all, the count now reaches the first store one `add` after the mask. The
; whole 9-character call also stays on the FALL-THROUGH path: every branch it executes is
; not-taken, including the fill rung, because the ladder is ordered widest-first.
;
; THE BROADCAST: the fill value is splatted with `vmovd` + `vpbroadcastw` into an xmm, not built in
; a GPR. 182 uses `imul r8, 0x0101010101010101`, which is exact only because c <= 255 keeps every
; partial product inside its own byte lane; the wide argument is a full wchar_t and can be 0xFFFF,
; so the GPR form here would need 0x0001000100010001 (exact for any 16-bit c, since its set bits
; are 16 apart) and then FOUR `mov` stores to cover the common 16..28 byte span. One VEX-128
; broadcast plus TWO 16-byte stores is shorter, and it costs no upper state. r8 is left untouched,
; so the one-character rung can still store r8w directly. correctness.c sweeps 0x0000, the
; surrogate range and 0xFFFF through this path.
;
; CONTRACT, unchanged, and unusual enough to restate. The `_s` family in this CRT has three
; distinct shapes and assuming one from another is how an earlier candidate was refuted on 407 604
; of 1 000 000 cases. The parent pinned this one by probing the live export
; (../182-strset-s/probes/sss.c, which fuzzed the byte AND the wide form, 1 000 000 cases each,
; 0 mismatches):
;
;   * numberOfElements == 0  -> EINVAL (22) and nothing is written at all. Note that this is a
;     count of ELEMENTS, not bytes, and so is every other count in this contract.
;   * No terminator strictly inside numberOfElements -> PARTIAL FILL of numberOfElements-1 cells,
;     and only THEN str[0] = 0, returning EINVAL (22).
;   * Otherwise -> fill every cell before the terminator, keep the terminator, return 0.
;   * Every fill value behaves the same, including 0, the surrogate range and 0xFFFF.
;
; PAGE SAFETY, the repository's two-part discipline, restated for a probe pair:
;   * DECLARED BUFFER. Both probes read strictly inside [str, str + 2*numberOfElements), because
;     the tail probe is placed against that span's end and the pair is only used when the span is
;     at least as wide as one probe. No read goes past what the caller declared.
;   * PAGE. A caller may declare more than it allocated, so the declared span is additionally
;     required to lie inside a single page. Because both probes live inside that span, ONE test --
;     first and last byte agree above bit 11, guards them both, which is why the pair costs one
;     guard and not two. When it fails, the call falls back to a one-character walk that never
;     returns to a probe, so no character is probed twice and none is read that the parent would
;     not have read.
;   * The fill writes at most numberOfElements-1 characters; the overlapping stores are placed
;     against the two ends of the span [0, count) bytes, so they never run past the count and in
;     particular never overwrite the terminator the scan just found.
;
; ISA: AVX2 for the >= 16 character path (the parent's), VEX-128 + BMI1 + general-purpose below.
; No AVX-512; this part has it, but a 128-bit EVEX form of the same scan buys nothing and the
; k-register round trip needed to turn a compare into an index is longer than `vpmovmskb`.
; Validated on bench #3 (Intel i9-11900H, Tiger Lake-H), see docs/PLATFORM-i9-11900H.md.

EXTERN _invalid_parameter_noinfo:PROC

.code
wia_wcsset_s PROC
        test      rdx, rdx
        jz        err_nowrite                    ; bound 0: EINVAL, nothing written
        cmp       rdx, 16
        jae       wide                           ; 16 characters = 32 bytes: only now is dirtying
                                                 ; a ymm, and owing a vzeroupper, worth it

;=========== narrow path: bound < 16 characters. No ymm is written anywhere below. ===========
; The whole budget is under 16 characters, so the fill count, the length on success, or
; numberOfElements-1 on failure, is at most 28 BYTES, and the ladder at n_fill covers every case.
; r10 carries that count in bytes and r11 carries the outcome (0 = success, 1 = EINVAL).

        vmovd     xmm2, r8d                      ; the fill value, splatted now: it depends only on
        vpbroadcastw xmm2, xmm2                  ; r8, so it is off the scan's dependency chain

        lea       rax, [rcx + rdx*2 - 1]         ; last byte of the DECLARED span
        xor       rax, rcx                       ; same page as the first byte?
        cmp       rax, 4096
        jae       n_scan1                        ; no: one test has rejected both probes at once
        cmp       rdx, 8
        jb        n_probe4

        ; ---- 8 <= bound <= 15: two 16-byte probes cover the whole bound, head and tail ----
        lea       r9, [rdx*2 - 16]               ; byte offset of the tail probe
        vpxor     xmm1, xmm1, xmm1
        vmovdqu   xmm0, xmmword ptr [rcx]        ; characters 0..7
        vmovdqu   xmm3, xmmword ptr [rcx + r9]   ; characters n-8..n-1
        vpcmpeqw  xmm0, xmm0, xmm1
        vpcmpeqw  xmm3, xmm3, xmm1
        vpmovmskb eax, xmm0
        vpmovmskb r10d, xmm3
        test      eax, eax
        jnz       n_head                         ; a terminator below character 8 wins outright
        test      r10d, r10d
        jz        n_noterm
        tzcnt     r10d, r10d                     ; the head mask was empty, so the lowest bit of
        add       r10, r9                        ; the tail mask IS the first terminator
        xor       r11d, r11d                     ; outcome = success

;---- fill: overlapping stores, count in BYTES, widest rung on the fall-through ----
n_fill:                                          ; r10 = bytes to fill (0..28, even), rcx = base
        cmp       r10, 16
        jb        nf_under16
        vmovdqu   xmmword ptr [rcx], xmm2        ; 16..28 -- the bench's "8" class lands here
        vmovdqu   xmmword ptr [rcx + r10 - 16], xmm2
n_done:
        test      r11d, r11d                     ; no vzeroupper: the upper state was never dirtied
        jnz       err_after_fill
        xor       eax, eax                       ; success
        ret

nf_under16:
        cmp       r10, 8
        jb        nf_under8
        vmovq     qword ptr [rcx], xmm2          ; 8..14
        vmovq     qword ptr [rcx + r10 - 8], xmm2
        jmp       n_done
nf_under8:
        cmp       r10, 4
        jb        nf_under4
        vmovd     dword ptr [rcx], xmm2          ; 4..6
        vmovd     dword ptr [rcx + r10 - 4], xmm2
        jmp       n_done
nf_under4:
        test      r10, r10
        jz        n_done
        mov       word ptr [rcx], r8w            ; 2 -- r8 was never used as scratch
        jmp       n_done

n_head:                                          ; terminator inside the head probe
        tzcnt     r10d, eax                      ; vpcmpeqw sets BOTH bytes of a matching word, so
        xor       r11d, r11d                     ; the low set bit is the word's low byte, and the
        jmp       n_fill                         ; byte offset IS the count already

n_noterm:
        lea       r10, [rdx*2 - 2]               ; count = numberOfElements - 1 characters
        mov       r11d, 1                        ; outcome = EINVAL
        jmp       n_fill

        ; ---- 4 <= bound <= 7: the same pair, eight bytes wide ----
n_probe4:
        cmp       rdx, 4
        jb        n_scan1
        lea       r9, [rdx*2 - 8]
        vpxor     xmm1, xmm1, xmm1
        vmovq     xmm0, qword ptr [rcx]          ; characters 0..3; the load zero-extends, so words
        vmovq     xmm3, qword ptr [rcx + r9]     ; 4..7 read as terminators and the masks are
        vpcmpeqw  xmm0, xmm0, xmm1               ; trimmed to the eight bytes actually LOADED
        vpcmpeqw  xmm3, xmm3, xmm1
        vpmovmskb eax, xmm0
        vpmovmskb r10d, xmm3
        and       eax, 0FFh
        jnz       n_head
        and       r10d, 0FFh
        jz        n_noterm
        tzcnt     r10d, r10d
        add       r10, r9
        xor       r11d, r11d
        jmp       n_fill

n_scan1:                                         ; bound < 4, or a span that straddles a page
        mov       r9, rcx                        ; cursor
        mov       rax, rdx                       ; characters of budget remaining
n_step:
        test      rax, rax
        jz        n_noterm
        cmp       word ptr [r9], 0
        je        n_found
        add       r9, 2
        dec       rax
        jmp       n_step
n_found:
        sub       r9, rcx                        ; count = length, already in bytes
        mov       r10, r9
        xor       r11d, r11d                     ; outcome = success
        jmp       n_fill

;========= wide path: bound >= 16 characters, the parent's code, spliced in unchanged =========
wide:
        mov       r9, rcx                        ; scan cursor
        mov       r10, rdx                       ; characters of budget remaining
        vpxor     ymm1, ymm1, ymm1

        ;================ bounded terminator scan ================
scan:
        cmp       r10, 16
        jb        scan_tail
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        scan_step
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       scan_hit
        add       r9, 32
        sub       r10, 16
        jmp       scan
scan_step:                                       ; near a page end: one character, then retry
        cmp       word ptr [r9], 0
        je        scan_found
        add       r9, 2
        dec       r10
        jnz       scan
        jmp       no_term
scan_tail:
        test      r10, r10
        jz        no_term
        cmp       word ptr [r9], 0
        je        scan_found
        add       r9, 2
        dec       r10
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax                       ; vpcmpeqw sets BOTH bytes of a matching word,
        add       r9, rax                        ; so the low set bit is the word's low byte
scan_found:
        sub       r9, rcx
        shr       r9, 1                          ; bytes -> characters
        mov       r11, r9                        ; count = length
        xor       r10d, r10d                     ; outcome = success
        jmp       do_fill
no_term:
        lea       r11, [rdx - 1]                 ; count = numberOfElements - 1
        mov       r10d, 1                        ; outcome = EINVAL

        ;================ one fill loop serves both outcomes ================
do_fill:
        movzx     eax, r8w
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        mov       r9, rcx                        ; fill cursor
f_blk:
        cmp       r11, 16
        jb        f_tail
        vmovdqu   ymmword ptr [r9], ymm2
        add       r9, 32
        sub       r11, 16
        jmp       f_blk
f_tail:
        test      r11, r11
        jz        f_done
        mov       word ptr [r9], r8w
        add       r9, 2
        dec       r11
        jmp       f_tail
f_done:
        vzeroupper
        test      r10d, r10d
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
wia_wcsset_s ENDP
END
