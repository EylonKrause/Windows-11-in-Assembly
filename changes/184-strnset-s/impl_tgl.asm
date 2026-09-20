; changes/184-strnset-s/impl_tgl.asm
; errno_t wia_strnset_s(char* str, size_t numberOfElements, int c, size_t count)
;   [Win64: rcx, rdx, r8d, r9 -> eax]
;
; Tiger Lake / Willow Cove variant of change 184. Same contract, same oracle, same gates; this is
; the only file that differs from the parent.
;
; Why a variant and not an edit
; -----------------------------
; The parent wins every size class on Zen 3 and Zen 4. Here the smallest class measures 0.79x, and
; the reason is not the arithmetic; it is that at that size the parent does no vector work at all
; while paying the full price of having intended to. The bench's "8" row is 8 BYTES: an 8-character
; string with numberOfElements = 9 and count = _TRUNCATE, so the terminator sits at index 8 and the
; fill is min(count, 8) = 8 bytes. Against that input the parent:
;
;   * runs `vpxor ymm1, ymm1, ymm1` unconditionally in the prologue, before the bound is examined;
;   * takes `cmp r11, 32 / jb scan_tail` on the scan's very first test and then walks all nine
;     bytes of the bounded scan one at a time;
;   * runs `vpbroadcastb ymm2` unconditionally, then falls straight through `f_blk` into `f_tail`
;     and stores the eight fill bytes one at a time;
;   * and, because both ymm writes have dirtied the upper state, owes a `vzeroupper` on the way
;     out. That is mandatory, without it the caller's next SSE instruction pays a transition
;     penalty, and it bought nothing here.
;
; So the short case pays for two ymm writes, a vzeroupper and seventeen single-byte iterations in
; order to move eight bytes. Willow Cove's dirty-upper bookkeeping is dearer than Zen's, which is
; why the same code is a win on bench #1 and a loss here.
;
; What this variant changes
; -------------------------
; It splits at the bound BEFORE touching any vector register:
;
;   * Below a 32-byte bound it stays in VEX-128 and general-purpose registers. A VEX-encoded
;     128-bit instruction zeroes bits 128 and above of its destination, so the upper state stays
;     CLEAN and no `vzeroupper` is owed on that path at all.
;   * The scan probes 16 bytes, then 8, instead of walking. At numberOfElements = 9 the terminator
;     is found by one 8-byte probe plus one byte compare rather than by nine byte compares.
;   * The fill uses OVERLAPPING stores rather than a byte loop, writing the same byte twice is
;     free, branching once per byte is not.
;   * The broadcast is built with an `imul` against 0101010101010101h, so no vector register is
;     involved at all. For c <= 255 the partial products c<<0, c<<8, ... cannot carry into one
;     another, so every byte of the result is exactly c, and r8b still holds c afterwards, which
;     the 1-byte case uses directly.
;
; At or above a 32-byte bound it branches to the parent's code, spliced in unchanged. That is where
; the parent already wins up to 8.2x here, and it is not what this variant is about.
;
; One thing that went wrong, and is worth knowing
; -----------------------------------------------
; Inserting the narrow path AHEAD of the parent's code cost the parent's code 15-20% on two classes
; it had no business losing, 254 and 254/partial, both of which execute only spliced instructions.
; Nothing about them had changed; they had simply been pushed to a different offset, and the wide
; scan loop no longer began on a 16-byte boundary. An `ALIGN 16` in front of `wide:` gave both rows
; back (254/partial 45.9 ns against 45.1 ns for the parent, 254 actually better at 44.1 against
; 50.7). The directive emits padding only; it does not add, remove or reorder a single one of the
; parent's instructions, which is the property that makes the splice worth anything.
;
; This is the trap a variant of this shape walks straight into: a path you did not touch can still
; regress, because you moved it. A variant must be measured on every class, not on the one it set
; out to fix. Note also that `ALIGN 32` is not available, MASM rejects it here with A2189, because
; a request may not exceed the segment's own alignment and `.code` is 16.
;
; Why the split is on the bound and not on the fill length
; -------------------------------------------------------
; This function has two limits and only one of them is known before the scan. `count` can be
; _TRUNCATE while the bound is 9, or 1000 while the bound is 128, so the fill length is not a
; dispatch key. The BOUND is, and it caps both halves of the work: it bounds the scan directly, and
; it bounds the fill too, because the fill length is min(count, length) on success and
; min(count, numberOfElements-1) on failure, and both of those are strictly below the bound. A
; bound under 32 therefore guarantees a fill of at most 30 bytes, which is exactly the span the
; small overlapping-store ladder covers. One test on rdx decides the shape of the whole call, and
; it is a test the parent already had to make later anyway.
;
; The scan still cannot be shortened by `count`, on either path. Even when count is 0 the return
; value depends on whether a terminator exists strictly inside numberOfElements, so the bounded
; scan always runs to completion; only the fill is clipped. The narrow path keeps `count` live in
; r9 across the whole scan exactly as the parent does, and reuses r9 as the fill cursor only after
; the CMOV has consumed it.
;
; The parent is left untouched: its measurement was taken on hardware with different AVX
; transition costs, where dirtying the upper state for a nine-byte call is cheap enough not to show.
;
; CONTRACT, unchanged, and worth restating because the `_s` family in this CRT has three distinct
; shapes and assuming one from another is how a previous attempt on a sibling was refuted on 407604
; of 1000000 cases (see the parent's header and probes/sns.c for how this one was pinned):
;
;   * numberOfElements == 0 -> EINVAL (22) and nothing is written at all, whatever count says.
;   * No terminator strictly inside numberOfElements -> fill min(count, numberOfElements-1) cells,
;     and only THEN write str[0] = 0, returning EINVAL (22). The partial fill happens first and
;     stays observable in bytes 1..n-2; it is not undone.
;   * Otherwise -> fill min(count, length) cells, leave the rest of the string and the terminator
;     alone, return 0.
;   * count is in ELEMENTS, which for the narrow form is bytes; _TRUNCATE ((size_t)-1) is NOT
;     special-cased and simply saturates against the other limit.
;   * All 256 fill byte values behave identically, including 0.
;
; Page safety: a probe is issued only when that many bytes of the caller's declared buffer remain,
; and is additionally guarded against crossing into the next page, the same two-part discipline
; the parent uses, applied at 16 and 8 bytes as well as 32. When either test fails the narrow scan
; creeps a byte at a time to the end of the bound and never re-enters a probe loop, so no byte of
; input pays for a vector probe twice. The fill writes at most numberOfElements-1 bytes and the
; overlapping stores all land strictly inside that span, so the variant touches no byte the parent
; would not.
;
; ABI: rcx, rdx, r8, r9, r10, r11, rax and xmm0-xmm2 only, all volatile. Nothing in the
; non-volatile xmm6-xmm15 low-lane range is written on either path.
;
; ISA: AVX2 for the >= 32 path (the parent's), VEX-128 + GPR below. No AVX-512, although this bench
; has it. Two AVX-512 shapes were considered and rejected on reasoning, not measured: a masked
; `vmovdqu8 ymm{k}{z}` over the whole bound would buy only the fault suppression that the
; declared-bytes test already provides, while dirtying the upper state and so re-owing the
; `vzeroupper` this variant exists to remove; the EVEX-128 form keeps the state clean but pays a
; `bzhi`/`kmovd` set-up on what is the shortest path in the function. Replacing the 8-byte probe
; with the general-purpose zero-byte trick ((x-0101..h) & ~x & 8080..h) was also rejected: it
; shortens the chain by two or three cycles but needs two 64-bit constants and a scratch register,
; and at this size the call is dominated by fixed overhead both sides pay, so the registers would
; have been spent for something the bench cannot see.
; Validated on bench #3 (Intel i9-11900H, Tiger Lake-H), see docs/PLATFORM-i9-11900H.md.

EXTERN _invalid_parameter_noinfo:PROC

.code
wia_strnset_s PROC
        test      rdx, rdx
        jz        err_nowrite                    ; bound 0: EINVAL, nothing written
        mov       r10, rcx                       ; scan cursor
        mov       r11, rdx                       ; bytes of budget remaining
        cmp       rdx, 32
        jae       wide                           ; only now is dirtying a ymm worth it

;================ narrow path: bound < 32. No ymm is written anywhere below. ================
; r9 holds `count` untouched through the scan, exactly as on the wide path.
n_scan16:
        cmp       r11, 16
        jb        n_scan8
        mov       eax, r10d
        and       eax, 4095
        cmp       eax, 4080                      ; 16-byte read must stay inside this page
        ja        n_scan1
        vmovdqu   xmm0, xmmword ptr [r10]
        vpxor     xmm1, xmm1, xmm1
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        test      eax, eax
        jnz       n_hit
        add       r10, 16
        sub       r11, 16
        jmp       n_scan16
n_scan8:
        cmp       r11, 8
        jb        n_scan1
        mov       eax, r10d
        and       eax, 4095
        cmp       eax, 4088                      ; 8-byte read must stay inside this page
        ja        n_scan1
        vmovq     xmm0, qword ptr [r10]
        vpxor     xmm1, xmm1, xmm1
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        and       eax, 0FFh                      ; only the eight bytes actually loaded count
        test      eax, eax
        jnz       n_hit
        add       r10, 8
        sub       r11, 8
        jmp       n_scan8
n_scan1:                                         ; fewer than 8 left, or hard against a page end
        test      r11, r11
        jz        n_noterm
        cmp       byte ptr [r10], 0
        je        n_found
        inc       r10
        dec       r11
        jmp       n_scan1
n_hit:
        tzcnt     eax, eax
        add       r10, rax
n_found:
        sub       r10, rcx                       ; r10 = length
        cmp       r10, r9
        cmova     r10, r9                        ; r10 = min(count, length)
        xor       edx, edx                       ; outcome = success
        jmp       n_fill
n_noterm:
        lea       r10, [rdx - 1]                 ; numberOfElements - 1
        cmp       r10, r9
        cmova     r10, r9                        ; r10 = min(count, numberOfElements-1)
        mov       edx, 1                         ; outcome = EINVAL

n_fill:
        ; The limit is at most 30 here (see "why the split is on the bound" above) so this
        ; ladder covers every reachable case with at most four stores and no loop at all.
        movzx     eax, r8b
        mov       r8, 0101010101010101h
        imul      r8, rax                        ; every byte of r8 is now c; r8b is still c
        mov       r9, rcx                        ; fill cursor (count has been consumed)
        cmp       r10, 16
        jae       nf_16_31
        cmp       r10, 8
        jae       nf_8_15
        cmp       r10, 4
        jae       nf_4_7
        cmp       r10, 2
        jae       nf_2_3
        test      r10, r10
        jz        n_done
        mov       byte ptr [r9], r8b
        jmp       n_done
nf_2_3:
        mov       word ptr [r9], r8w
        mov       word ptr [r9 + r10 - 2], r8w
        jmp       n_done
nf_4_7:
        mov       dword ptr [r9], r8d
        mov       dword ptr [r9 + r10 - 4], r8d
        jmp       n_done
nf_8_15:
        mov       qword ptr [r9], r8
        mov       qword ptr [r9 + r10 - 8], r8
        jmp       n_done
nf_16_31:
        mov       qword ptr [r9], r8
        mov       qword ptr [r9 + 8], r8
        mov       qword ptr [r9 + r10 - 16], r8
        mov       qword ptr [r9 + r10 - 8], r8
n_done:
        test      edx, edx                       ; no vzeroupper: the upper state was never dirtied
        jnz       err_after_fill
        xor       eax, eax                       ; success
        ret

;================ wide path: the parent's code, spliced in unchanged ================
        ALIGN     16
wide:
        vpxor     ymm1, ymm1, ymm1               ; (r9 holds `count` throughout the scan)

        ;---------------- bounded terminator scan ----------------
scan:
        cmp       r11, 32
        jb        scan_tail
        mov       eax, r10d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        scan_step
        vmovdqu   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       scan_hit
        add       r10, 32
        sub       r11, 32
        jmp       scan
scan_step:                                       ; near a page end: one byte, then retry
        cmp       byte ptr [r10], 0
        je        scan_found
        inc       r10
        dec       r11
        jnz       scan
        jmp       no_term
scan_tail:
        test      r11, r11
        jz        no_term
        cmp       byte ptr [r10], 0
        je        scan_found
        inc       r10
        dec       r11
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax
        add       r10, rax
scan_found:
        sub       r10, rcx                       ; r10 = length
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
        movzx     eax, r8b
        vmovd     xmm2, eax
        vpbroadcastb ymm2, xmm2
        mov       r9, rcx                        ; fill cursor (count is no longer needed)
f_blk:
        cmp       r10, 32
        jb        f_tail
        vmovdqu   ymmword ptr [r9], ymm2
        add       r9, 32
        sub       r10, 32
        jmp       f_blk
f_tail:
        test      r10, r10
        jz        f_done
        mov       byte ptr [r9], r8b
        inc       r9
        dec       r10
        jmp       f_tail
f_done:
        vzeroupper
        test      edx, edx
        jnz       err_after_fill
        xor       eax, eax                       ; success
        ret

err_after_fill:
        mov       byte ptr [rcx], 0              ; empty the string AFTER the partial fill
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
wia_strnset_s ENDP
END
