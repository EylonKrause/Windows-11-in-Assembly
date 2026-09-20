; changes/182-strset-s/impl_tgl.asm
; errno_t wia_strset_s(char* str, size_t numberOfElements, int c)
;   [Win64: rcx, rdx, r8d -> eax]
;
; Tiger Lake / Willow Cove variant of change 182. Same contract, same oracle, same gates; this is
; the only file that differs from the parent.
;
; Why a variant and not an edit
; -----------------------------
; The parent wins every size class on Zen 3. Here the 8-byte class measures 0.83x, and the reason
; is that at that size the parent does no vector work at all while paying the full price of having
; intended to:
;
;   * `vpxor ymm1, ymm1, ymm1` runs unconditionally in the prologue, before the bound is examined.
;   * With numberOfElements = 9, the bounded scan takes `cmp r10, 32 / jb scan_tail` on its first
;     test and walks eight bytes one at a time.
;   * `vpbroadcastb ymm2` then runs unconditionally, and the fill likewise falls straight to
;     `f_tail` and stores eight bytes one at a time.
;   * Both ymm writes have dirtied the upper state, so the epilogue owes a `vzeroupper`, which is
;     mandatory (the caller's later SSE code would otherwise pay a transition penalty) and bought
;     nothing here.
;
; So the short case pays for two ymm writes, a vzeroupper, and sixteen single-byte iterations. The
; variant splits at the bound BEFORE touching any vector register:
;
;   * Below a 32-byte bound it stays in VEX-128 and general-purpose registers. A VEX-encoded
;     128-bit instruction zeroes bits 128 and above of its destination, so the upper state stays
;     CLEAN and no `vzeroupper` is owed on that path at all.
;   * The scan probes 16 bytes, then 8, instead of walking. At numberOfElements = 9 the terminator
;     is found by a single 8-byte probe rather than eight compares.
;   * The fill uses OVERLAPPING stores rather than a byte loop, writing the same byte twice is
;     free, branching once per byte is not. The broadcast is built with an imul against
;     0x0101010101010101, so no vector register is involved.
;
; At or above a 32-byte bound it branches to the parent's code, spliced in unchanged. That is where
; the parent already wins up to 12.9x, and it is not what this variant is about.
;
; The parent is left untouched: its measurement was taken on hardware with different AVX transition
; costs, where dirtying the upper state for a short call is cheap enough not to show.
;
; CONTRACT, unchanged, and it is unusual enough to restate, because two of the three `_s` shapes
; in this CRT would be wrong here (see the parent's header for how it was pinned):
;
;   * numberOfElements == 0 -> EINVAL (22) and nothing is written.
;   * No terminator strictly inside numberOfElements -> PARTIAL FILL of numberOfElements-1 cells,
;     THEN str[0] = 0, return EINVAL (22).
;   * Otherwise -> fill every cell before the terminator, keep the terminator, return 0.
;   * All 256 fill byte values behave the same, including 0.
;
; Page safety: a probe is issued only when that many bytes of the caller's declared buffer remain,
; and is additionally guarded against crossing into the next page, the same two-part discipline
; the parent uses, applied at 16 and 8 bytes as well as 32. The fill writes at most
; numberOfElements-1 bytes, and the overlapping stores land strictly inside that span, so the
; variant touches no byte the parent would not.
;
; ISA: AVX2 for the >= 32 path (the parent's), VEX-128 + GPR below. Validated on bench #3
; (Intel i9-11900H, Tiger Lake-H), see docs/PLATFORM-i9-11900H.md.

EXTERN _invalid_parameter_noinfo:PROC

.code
wia_strset_s PROC
        test      rdx, rdx
        jz        err_nowrite                    ; bound 0: EINVAL, nothing written
        mov       r9, rcx                        ; scan cursor
        mov       r10, rdx                       ; bytes of budget remaining
        cmp       rdx, 32
        jae       wide                           ; only now is dirtying a ymm worth it

;================ narrow path: bound < 32. No ymm is written anywhere below. ================
; The whole budget is under 32 bytes, so the fill count, length on success, numberOfElements-1
; on failure, is at most 30 and the small ladder covers every case.
n_scan16:
        cmp       r10, 16
        jb        n_scan8
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4080                      ; 16-byte read must stay inside this page
        ja        n_scan1
        vmovdqu   xmm0, xmmword ptr [r9]
        vpxor     xmm1, xmm1, xmm1
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        test      eax, eax
        jnz       n_hit
        add       r9, 16
        sub       r10, 16
        jmp       n_scan16
n_scan8:
        cmp       r10, 8
        jb        n_scan1
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4088                      ; 8-byte read must stay inside this page
        ja        n_scan1
        vmovq     xmm0, qword ptr [r9]
        vpxor     xmm1, xmm1, xmm1
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        and       eax, 0FFh                      ; only the eight bytes actually loaded count
        test      eax, eax
        jnz       n_hit
        add       r9, 8
        sub       r10, 8
        jmp       n_scan8
n_scan1:                                         ; fewer than 8 left, or hard against a page end
        test      r10, r10
        jz        n_noterm
        cmp       byte ptr [r9], 0
        je        n_found
        inc       r9
        dec       r10
        jmp       n_scan1
n_hit:
        tzcnt     eax, eax
        add       r9, rax
n_found:
        sub       r9, rcx                        ; count = length
        mov       r11, r9
        xor       r10d, r10d                     ; outcome = success
        jmp       n_fill
n_noterm:
        lea       r11, [rdx - 1]                 ; count = numberOfElements - 1
        mov       r10d, 1                        ; outcome = EINVAL

n_fill:
        ; Broadcast the fill byte across a qword without touching a vector register. For c <= 255
        ; the products c<<0, c<<8, ... never carry into one another, so every byte is exactly c --
        ; and r8b still holds c afterwards, which the 1-byte case uses.
        movzx     eax, r8b
        mov       r8, 0101010101010101h
        imul      r8, rax
        mov       r9, rcx                        ; fill cursor
        cmp       r11, 16
        jae       nf_16_31
        cmp       r11, 8
        jae       nf_8_15
        cmp       r11, 4
        jae       nf_4_7
        cmp       r11, 2
        jae       nf_2_3
        test      r11, r11
        jz        n_done
        mov       byte ptr [r9], r8b
        jmp       n_done
nf_2_3:
        mov       word ptr [r9], r8w
        mov       word ptr [r9 + r11 - 2], r8w
        jmp       n_done
nf_4_7:
        mov       dword ptr [r9], r8d
        mov       dword ptr [r9 + r11 - 4], r8d
        jmp       n_done
nf_8_15:
        mov       qword ptr [r9], r8
        mov       qword ptr [r9 + r11 - 8], r8
        jmp       n_done
nf_16_31:
        mov       qword ptr [r9], r8
        mov       qword ptr [r9 + 8], r8
        mov       qword ptr [r9 + r11 - 16], r8
        mov       qword ptr [r9 + r11 - 8], r8
n_done:
        test      r10d, r10d                     ; no vzeroupper: the upper state was never dirtied
        jnz       err_after_fill
        xor       eax, eax
        ret

;================ wide path: the parent's code, unchanged ================
wide:
        vpxor     ymm1, ymm1, ymm1

        ;---------------- bounded terminator scan ----------------
scan:
        cmp       r10, 32
        jb        scan_tail
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        scan_step
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       scan_hit
        add       r9, 32
        sub       r10, 32
        jmp       scan
scan_step:                                       ; near a page end: one byte, then retry
        cmp       byte ptr [r9], 0
        je        scan_found
        inc       r9
        dec       r10
        jnz       scan
        jmp       no_term
scan_tail:
        test      r10, r10
        jz        no_term
        cmp       byte ptr [r9], 0
        je        scan_found
        inc       r9
        dec       r10
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax
        add       r9, rax
scan_found:
        sub       r9, rcx
        mov       r11, r9                        ; count = length
        xor       r10d, r10d                     ; outcome = success
        jmp       do_fill
no_term:
        lea       r11, [rdx - 1]                 ; count = numberOfElements - 1
        mov       r10d, 1                        ; outcome = EINVAL

        ;---------------- one fill loop serves both outcomes ----------------
do_fill:
        movzx     eax, r8b
        vmovd     xmm2, eax
        vpbroadcastb ymm2, xmm2
        mov       r9, rcx                        ; fill cursor
f_blk:
        cmp       r11, 32
        jb        f_tail
        vmovdqu   ymmword ptr [r9], ymm2
        add       r9, 32
        sub       r11, 32
        jmp       f_blk
f_tail:
        test      r11, r11
        jz        f_done
        mov       byte ptr [r9], r8b
        inc       r9
        dec       r11
        jmp       f_tail
f_done:
        vzeroupper
        test      r10d, r10d
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
wia_strset_s ENDP
END
