; changes/008-rtlcompareunicodestring/impl_tgl.asm
; LONG wia_rtlcmpustr(const UNICODE_STRING* s1, const UNICODE_STRING* s2, BOOLEAN ci)
;   [Win64: rcx, rdx, r8b -> eax]
;
; Tiger Lake / Willow Cove variant of change 008. Same contract, same oracle, same gates. Only the
; SHORT CASE-INSENSITIVE path differs; every other path below is the parent's, spliced in unchanged.
;
; Why a variant and not an edit
; -----------------------------
; The parent wins every class on Zen 3. Here the 8-wchar case-insensitive class measures 0.94x
; against ntdll while the change still wins 3.2x overall. Below 16 wchars the parent takes
; `ci_small`, a scalar walk costing FOUR loads per character in two DEPENDENT pairs -- load the
; wchar, then index the OS upcase table with the value just loaded, for each side. The second load
; of each pair cannot issue until the first retires, and at eight characters there is nothing else
; in flight to hide sixteen such latencies behind.
;
; The first attempt at this was worse, and why that is worth recording
; --------------------------------------------------------------------
; The obvious fix is to compare the two RAW wchars first and consult the table only when they
; differ. That is sound -- upcase is a function, so bit-identical inputs have bit-identical folds.
; Written as a per-character test it measured **0.78x**: worse than the parent it was meant to fix.
;
; The saving was real; the shape was not. Skipping the table put the common case behind a TAKEN
; branch into the loop tail, so every matching character paid two taken branches where the parent
; paid one. Two loads saved, one extra taken branch spent, net loss. The lesson is not that the
; idea was wrong -- it is that at eight characters this function is branch-bound rather than
; load-bound, and only a measurement says which.
;
; So the unit is the BLOCK, not the character. One 128-bit load pair covers eight wchars, and a
; single `vpcmpeqw` plus a mask compare answers "is this whole block identical?" with no per-
; character branch at all. Raw-equal implies fold-equal, so an identical block needs no case table,
; no ASCII test and no folding. Only a block that DIFFERS pays for any of that.
;
; Why 128-BIT and not 256
; -----------------------
; The parent's vector path is 256-bit and needs five ymm constants, which forces it to spill
; ymm6/ymm7 -- 64 bytes of stack -- because Win64 preserves the low 128 bits of xmm6-xmm15. At
; eight wchars that spill costs more than the extra width earns. At 128 bits the constants fit in
; xmm0-xmm5, which are volatile. So the short path saves nothing to the stack, and being
; VEX/EVEX-128 throughout it never dirties the upper state and owes no `vzeroupper`.
;
; The fold itself is where this machine pays off. On AVX-512 a compare writes a MASK REGISTER, and
; the subtract can be predicated on it -- so `islower` costs two compares, one `kandw` and a masked
; `vpsubw` In place, with no and of two 128-bit compare results and no temporary vector register to
; hold them. The parent needs five vector operations per side and a spare register for each; this
; needs four and none. k0-k7 are volatile under Win64, so again nothing is saved.
;
; (An earlier draft tried to keep the parent's shape and put the two compare results in xmm16/xmm17.
; MASM rejects it, correctly: Vpcmpgtw has no evex form that writes a vector -- the evex encoding
; IS the mask-writing one, and the VEX form cannot reach xmm16-31. The instruction set was pointing
; at the better sequence.)
;
; Exactness is unchanged. a differing block is still resolved through `wia_upcase`, the table built
; once from the OS, so the answer is bit-exact rather than an ASCII approximation. The vectorized
; ASCII fold runs only after `vptest` has proved every wchar in both blocks is < 0x80 -- the same
; guard the parent's 256-bit path uses -- and a pair that survives the fold as different is
; resolved through the table, so the returned magnitude comes from the source it always did.
;
; The parent is left untouched: its numbers were taken on a machine with no AVX-512 at all and
; different branch and AVX-transition costs, and they remain the record there.
;
; ISA: AVX2 for the parent's paths; VEX-128 plus AVX512BW+VL (mask registers k1/k2 and a
; masked 128-bit vpsubw) for the short CI path.
; Validated on bench #3 (Intel i9-11900H, Tiger Lake-H) -- see docs/PLATFORM-i9-11900H.md.
;
; UNICODE_STRING: Length @+0 (u16 BYTES), MaximumLength @+2, Buffer @+8. Length is in BYTES, so the
; bench row labelled "8/CI" is 16 bytes -- eight wchars, exactly one 128-bit block.

EXTERN wia_upcase:WORD

.const
ALIGN 16
C0060   DW      16 dup(0060h)
C007B   DW      16 dup(007Bh)
C0020   DW      16 dup(0020h)
CFF80   DW      16 dup(0FF80h)

.code
wia_rtlcmpustr PROC
        ; ---- header read, entirely in volatile registers ------------------------------------
        ; Nothing here touches rbx/rsi/rdi/r12/r13: the short CI path must reach its answer without
        ; ever having saved them.
        movzx     r9d, word ptr [rcx]              ; Length1 (bytes)
        movzx     r10d, word ptr [rdx]             ; Length2 (bytes)
        mov       r11, [rcx + 8]                   ; Buffer1
        mov       rcx, [rdx + 8]                   ; Buffer2   (rcx free from here)
        mov       eax, r9d
        sub       eax, r10d                        ; lenDiff = L1 - L2, the prefix-equal answer
        cmp       r9d, r10d
        cmovg     r9d, r10d                        ; r9d = common = min(L1,L2) bytes

        test      r8b, r8b
        jz        heavy                            ; case-sensitive -> the parent's path
        cmp       r9d, 32
        jae       heavy                            ; long enough for the parent's 256-bit fold

; ================ short, case-insensitive: the only path this variant changes ================
; Live: r11 = Buffer1, rcx = Buffer2, r9d = common bytes, eax = lenDiff, rdx = offset,
;       r8d/r10d = scratch. No callee-saved register is touched unless the table is needed.
        xor       edx, edx
nci_blk:
        mov       r8d, r9d
        sub       r8d, edx
        cmp       r8d, 16
        jb        nci_scalar                       ; fewer than 8 wchars left

        vmovdqu   xmm0, xmmword ptr [r11 + rdx]
        vmovdqu   xmm1, xmmword ptr [rcx + rdx]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb r8d, xmm2
        cmp       r8d, 0FFFFh
        je        nci_adv                          ; identical block -> identical folds

        ; The block differs. Only now does anything else get paid for.
        vpor      xmm2, xmm0, xmm1
        vpand     xmm2, xmm2, xmmword ptr [CFF80]
        vptest    xmm2, xmm2
        jnz       nci_scalar                       ; some wchar >= 0x80 -> the OS table decides

        vmovdqu   xmm3, xmmword ptr [C0060]
        vmovdqu   xmm4, xmmword ptr [C007B]
        vmovdqu   xmm5, xmmword ptr [C0020]
        ; AVX-512 does this in half the instructions the parent needs, because a compare writes a
        ; MASK and the subtract can then be predicated on it -- no AND of two 128-bit compare
        ; results, and no temporary vector register to hold them. k0-k7 are volatile under Win64.
        vpcmpgtw  k1, xmm0, xmm3                   ; a > 0x60
        vpcmpgtw  k2, xmm4, xmm0                   ; 0x7B > a
        kandw     k1, k1, k2                       ; islower(a)
        vpsubw    xmm0{k1}, xmm0, xmm5             ; fold in place, only where lower
        vpcmpgtw  k1, xmm1, xmm3
        vpcmpgtw  k2, xmm4, xmm1
        kandw     k1, k1, k2                       ; islower(b)
        vpsubw    xmm1{k1}, xmm1, xmm5
        vpcmpeqw  k1, xmm0, xmm1                   ; 8 words -> 8 mask bits
        kmovw     r8d, k1
        cmp       r8d, 0FFh
        je        nci_adv                          ; equal once folded
        not       r8d
        and       r8d, 0FFh
        tzcnt     r8d, r8d                         ; index of the first differing WORD
        lea       rdx, [rdx + r8*2]                ; -> byte offset
        jmp       nci_diff                         ; resolve it through the OS table, as always
nci_adv:
        add       rdx, 16
        jmp       nci_blk

nci_diff:                                          ; rdx = offset of the differing wchar
        push      rbx
        lea       rbx, wia_upcase
        movzx     r8d,  word ptr [r11 + rdx]
        movzx     r8d,  word ptr [rbx + r8*2]
        movzx     r10d, word ptr [rcx + rdx]
        movzx     r10d, word ptr [rbx + r10*2]
        pop       rbx
        mov       eax, r8d
        sub       eax, r10d                        ; sign is the contract; magnitude unspecified
        ret

nci_scalar:                                        ; the tail, or a block containing non-ASCII
        push      rbx
        lea       rbx, wia_upcase
ns_loop:
        cmp       edx, r9d
        jae       ns_out                           ; common exhausted; eax already holds lenDiff
        movzx     r8d,  word ptr [r11 + rdx]
        movzx     r8d,  word ptr [rbx + r8*2]
        movzx     r10d, word ptr [rcx + rdx]
        movzx     r10d, word ptr [rbx + r10*2]
        cmp       r8d, r10d
        jne       ns_sub
        add       edx, 2
        jmp       ns_loop                          ; one taken branch per character, as the parent
ns_sub:
        mov       eax, r8d
        sub       eax, r10d
ns_out:
        pop       rbx
        ret                                        ; no vzeroupper: only VEX/EVEX-128 was used

; ================ everything below is the parent's, reached via this shim ================
; The parent's body expects its own register assignment, so it is restored here rather than
; rewritten -- the case-sensitive scan and the 256-bit CI fold are not what this variant is about.
heavy:
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        mov       rbx, r11                         ; Buffer1
        mov       rsi, rcx                         ; Buffer2
        mov       edi, r9d                         ; common bytes
        mov       r12d, eax                        ; lenDiff
        xor       edx, edx                         ; offset
        test      r8b, r8b
        jnz       ci
        ; falls through into cs_scan

; ---------------- case-sensitive ----------------
cs_scan:
        mov       r8d, edi
        sub       r8d, edx
        cmp       r8d, 32
        jb        cs_tail
        vmovdqu   ymm0, ymmword ptr [rbx + rdx]
        vmovdqu   ymm1, ymmword ptr [rsi + rdx]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r8d, ymm2
        cmp       r8d, 0FFFFFFFFh
        jne       cs_diff
        add       rdx, 32
        jmp       cs_scan
cs_diff:
        not       r8d
        tzcnt     r8d, r8d
        add       rdx, r8
        movzx     eax, word ptr [rbx + rdx]
        movzx     r8d, word ptr [rsi + rdx]
        sub       eax, r8d
        jmp       epilogue
cs_tail:
        cmp       rdx, rdi
        jae       cs_prefix
        movzx     eax, word ptr [rbx + rdx]
        movzx     r8d, word ptr [rsi + rdx]
        cmp       eax, r8d
        jne       cs_tdiff
        add       rdx, 2
        jmp       cs_tail
cs_tdiff:
        sub       eax, r8d
        jmp       epilogue
cs_prefix:
        mov       eax, r12d
        jmp       epilogue

; ---------------- case-insensitive ----------------
ci:
        lea       r13, wia_upcase
        mov       r8d, edi
        cmp       r8d, 32
        jb        ci_small                         ; too short to vectorize: no ymm save
        sub       rsp, 64
        vmovdqu   ymmword ptr [rsp], ymm6          ; save ymm6/ymm7 (nonvolatile)
        vmovdqu   ymmword ptr [rsp+32], ymm7
        vmovdqu   ymm3, ymmword ptr [C0060]
        vmovdqu   ymm4, ymmword ptr [C007B]
        vmovdqu   ymm5, ymmword ptr [C0020]
civ:
        mov       r8d, edi
        sub       r8d, edx
        cmp       r8d, 32
        jb        ci_tail
        vmovdqu   ymm0, ymmword ptr [rbx + rdx]
        vmovdqu   ymm1, ymmword ptr [rsi + rdx]
        vpor      ymm2, ymm0, ymm1
        vpand     ymm2, ymm2, ymmword ptr [CFF80]
        vptest    ymm2, ymm2
        jnz       ci_scalar16                      ; some wchar >= 0x80 -> scalar block
        ; upcase a -> ymm2
        vpcmpgtw  ymm2, ymm0, ymm3                 ; a > 0x60
        vpcmpgtw  ymm6, ymm4, ymm0                 ; 0x7B > a
        vpand     ymm2, ymm2, ymm6
        vpand     ymm2, ymm2, ymm5                 ; islower_a & 0x20
        vpsubw    ymm2, ymm0, ymm2                 ; ua
        ; upcase b -> ymm6
        vpcmpgtw  ymm6, ymm1, ymm3
        vpcmpgtw  ymm7, ymm4, ymm1
        vpand     ymm6, ymm6, ymm7
        vpand     ymm6, ymm6, ymm5
        vpsubw    ymm6, ymm1, ymm6                 ; ub
        vpcmpeqw  ymm2, ymm2, ymm6
        vpmovmskb r8d, ymm2
        cmp       r8d, 0FFFFFFFFh
        jne       ci_diff_vec
        add       rdx, 32
        jmp       civ
ci_diff_vec:
        not       r8d
        tzcnt     r8d, r8d
        add       rdx, r8
        jmp       ci_diff_ret
ci_scalar16:
        mov       r9d, 16
ci_s16:
        movzx     eax, word ptr [rbx + rdx]
        movzx     eax, word ptr [r13 + rax*2]
        movzx     r8d, word ptr [rsi + rdx]
        movzx     r8d, word ptr [r13 + r8*2]
        cmp       eax, r8d
        jne       ci_ret_sub
        add       rdx, 2
        dec       r9d
        jnz       ci_s16
        jmp       civ
ci_tail:
        cmp       rdx, rdi
        jae       ci_prefix
        movzx     eax, word ptr [rbx + rdx]
        movzx     eax, word ptr [r13 + rax*2]
        movzx     r8d, word ptr [rsi + rdx]
        movzx     r8d, word ptr [r13 + r8*2]
        cmp       eax, r8d
        jne       ci_ret_sub
        add       rdx, 2
        jmp       ci_tail
ci_diff_ret:
        movzx     eax, word ptr [rbx + rdx]
        movzx     eax, word ptr [r13 + rax*2]
        movzx     r8d, word ptr [rsi + rdx]
        movzx     r8d, word ptr [r13 + r8*2]
ci_ret_sub:
        sub       eax, r8d
        jmp       ci_epilogue
ci_prefix:
        mov       eax, r12d
        jmp       ci_epilogue

ci_small:                                            ; common < 32 bytes: scalar CI, no ymm saved
        cmp       rdx, rdi
        jae       ci_small_prefix
        movzx     eax, word ptr [rbx + rdx]
        movzx     eax, word ptr [r13 + rax*2]
        movzx     r8d, word ptr [rsi + rdx]
        movzx     r8d, word ptr [r13 + r8*2]
        cmp       eax, r8d
        jne       ci_small_sub
        add       rdx, 2
        jmp       ci_small
ci_small_sub:
        sub       eax, r8d
        jmp       epilogue
ci_small_prefix:
        mov       eax, r12d
        jmp       epilogue
ci_epilogue:
        vmovdqu   ymm6, ymmword ptr [rsp]
        vmovdqu   ymm7, ymmword ptr [rsp+32]
        add       rsp, 64
        jmp       epilogue

epilogue:
        vzeroupper
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_rtlcmpustr ENDP
END
