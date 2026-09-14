; changes/008-rtlcompareunicodestring/impl_2ndpc.asm
;==============================================================================
; 2ND PC VARIANT  --  AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445
;==============================================================================
; The original `impl.asm` is UNTOUCHED and remains the 5950X (Zen 3)
; implementation of record. This is an ADDITIONAL variant tuned for the second
; PC. Same exported symbol (`wia_rtlcmpustr`), so this change's existing
; correctness.c and bench.c validate it unmodified -- build with build_2ndpc.bat.
;
; WHY A 2ND-PC VARIANT IS NEEDED
; ------------------------------
; Re-measured here, the Zen 3 implementation failed the gate on ONE of its
; twelve size classes -- the shortest case-insensitive compare:
;
;     size        ours ns   system ns   ratio   verdict
;     8/cs           4.89        9.21   1.88x   BETTER
;     32..32000/cs                      4.67x - 5.55x   BETTER
;     8/CI           4.39        4.17   0.95x   WORSE    <-- gate failure
;     32/CI          4.19        9.48   2.26x   BETTER
;     128..32000/CI                     3.13x - 3.95x   BETTER
;     geomean 3.438x => PARKED (a size class regressed)
;
; Confirmed stable, not noise: three repeat runs gave 0.97x / 0.95x / 0.95x.
;
; Cause: fixed prologue cost, not the compare itself. An 8-wchar CI compare
; takes the `ci_small` scalar path, which needs NO vector register and NO
; callee-saved register -- yet it still pays, on every call:
;     * five `push` + five `pop` (rbx, rsi, rdi, r12, r13), ten memory ops, and
;     * a `vzeroupper` in the shared epilogue even though this path never
;       touched a YMM register at all.
; That fixed cost is a few cycles out of ~17, which is exactly the ~5% by which
; this class misses the 0.97x gate.
;
; THE FIX
; -------
; A dedicated short-CI entry placed BEFORE the prologue that runs entirely in
; volatile registers -- zero pushes, zero pops, no vzeroupper. The register
; budget is made to fit by running the compare on a NEGATIVE INDEX: both buffer
; pointers are pre-advanced to buffer+common and the index counts up from
; -common to 0. That removes the separate length counter, freeing the one
; register needed to hold lenDiff. Allocation (all volatile, Win64):
;     r11 = Buffer1 + common     rcx = Buffer2 + common     r8 = wia_upcase base
;     rax = index (negative)     edx = lenDiff              r9d/r10d = temps
; Anything that is not (case-insensitive AND common < 32 bytes) falls through to
; the original body, reproduced below completely unchanged.
;
; CONTRACT PRESERVED EXACTLY
;   * Only the SIGN of the return is contractual (ntdll's magnitude is not
;     portable), but this path reproduces the original's magnitude too: on a
;     mismatch it returns upcase(c1) - upcase(c2) through the SAME
;     `wia_upcase[]` table the original uses -- the table built from the OS
;     itself, so folding stays bit-exact for every code unit including >= 0x80.
;   * When the common prefix is equal it returns lenDiff = Length1 - Length2,
;     identical to the original's `ci_small_prefix`.
;   * common == 0 (either string empty) returns lenDiff without any load.
;   * The loop terminates on `js` (index still negative), mirroring the
;     original's `cmp rdx,rdi / jae` so that an ODD Length behaves identically
;     rather than running away.
;
; SAFETY
;   * Reads only; never writes through either buffer. It reads exactly the same
;     addresses over the same range as the original scalar path, so it cannot
;     fault where the original would not.
;   * No AVX-512, no GFNI, no vector register at all on the added path -- it is
;     correct on the 5950X too, simply unnecessary there.
;
; LONG wia_rtlcmpustr(const UNICODE_STRING* s1, const UNICODE_STRING* s2, BOOLEAN ci)
;   [Win64: rcx, rdx, r8b -> eax]
;   UNICODE_STRING: Length @+0 (u16 bytes), Buffer @+8.
; ISA: AVX2 + BMI1 (unchanged body) + scalar fast entry.

EXTERN wia_upcase:WORD

.const
ALIGN 16
C0060   DW      16 dup(0060h)
C007B   DW      16 dup(007Bh)
C0020   DW      16 dup(0020h)
CFF80   DW      16 dup(0FF80h)

.code
wia_rtlcmpustr PROC
        ;----------------------------------------------------------------------
        ; 2ND PC fast entry: case-insensitive AND common < 32 bytes.
        ; NO pushes, NO pops, NO vzeroupper.
        ;----------------------------------------------------------------------
        movzx     r9d, word ptr [rcx]              ; Length1 (bytes)
        movzx     r10d, word ptr [rdx]             ; Length2 (bytes)
        test      r8b, r8b
        jz        full_path                        ; case-sensitive -> original
        mov       eax, r9d
        cmp       eax, r10d
        cmovg     eax, r10d                        ; common = min(L1,L2) bytes
        cmp       eax, 32
        jae       full_path                        ; long -> original vector CI path

        mov       r11, [rcx + 8]                   ; Buffer1 (read before rcx is reused)
        mov       rcx, [rdx + 8]                   ; Buffer2 (s1 no longer needed)
        mov       edx, r9d
        sub       edx, r10d                        ; lenDiff = L1 - L2 (default result)
        lea       r8, wia_upcase                   ; ci flag consumed; r8 = table base
        add       r11, rax                         ; Buffer1 + common
        add       rcx, rax                         ; Buffer2 + common
        neg       rax                              ; index = -common .. 0
        jz        fast_prefix                      ; common == 0 -> lenDiff, no load
fast_loop:
        movzx     r9d,  word ptr [r11 + rax]       ; c1
        movzx     r9d,  word ptr [r8 + r9*2]       ; upcase(c1)
        movzx     r10d, word ptr [rcx + rax]       ; c2
        movzx     r10d, word ptr [r8 + r10*2]      ; upcase(c2)
        cmp       r9d, r10d
        jne       fast_diff
        add       rax, 2
        js        fast_loop                        ; still negative -> keep going
fast_prefix:
        mov       eax, edx                         ; common prefix equal -> lenDiff
        ret
fast_diff:
        mov       eax, r9d
        sub       eax, r10d                        ; upcase(c1) - upcase(c2)
        ret

        ;----------------------------------------------------------------------
        ; ORIGINAL BODY -- unchanged from impl.asm from here down.
        ;----------------------------------------------------------------------
full_path:
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        movzx     r9d, word ptr [rcx]              ; Length1 (bytes)
        movzx     r10d, word ptr [rdx]             ; Length2 (bytes)
        mov       rbx, [rcx + 8]                   ; Buffer1
        mov       rsi, [rdx + 8]                   ; Buffer2
        mov       r12d, r9d
        sub       r12d, r10d                       ; lenDiff = L1 - L2 (prefix-equal result)
        mov       edi, r9d
        cmp       edi, r10d
        cmovg     edi, r10d                        ; common = min(L1,L2) bytes
        xor       edx, edx                         ; offset
        test      r8b, r8b
        jnz       ci

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
