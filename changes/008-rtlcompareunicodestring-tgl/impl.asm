; changes/008-rtlcompareunicodestring-tgl/impl.asm
; LONG wia_rtlcmpustr(const UNICODE_STRING* s1, const UNICODE_STRING* s2, BOOLEAN ci)
;   [Win64: rcx, rdx, r8b -> eax]
;
; Tiger Lake / Willow Cove variant of change 008. Same contract, same oracle, same gates. Only the
; SHORT CASE-INSENSITIVE path differs; every other path below is the parent's code, spliced in
; unchanged.
;
; WHY A VARIANT AND NOT AN EDIT
; -----------------------------
; On Zen 3 the parent wins every size class, including 8 bytes case-insensitive, which is what its
; RESULTS.md records. Here that one class measures 0.94x against ntdll while the change still wins
; 3.2x overall. Eight bytes is FOUR wchars, so the measurement is almost entirely fixed overhead,
; and the parent pays two kinds of it before doing any useful work:
;
;   1. Five push/pop pairs in a prologue executed before the length is known. A four-character
;      compare never needs rbx/rsi/rdi/r12/r13, but pays to save and restore all five, plus a
;      `vzeroupper` in the shared epilogue that the scalar path did not earn.
;
;   2. A chain of two DEPENDENT loads per character. Upcasing goes through the OS-built table
;      (`wia_upcase`), so each character costs a load of the wchar and then a second load indexed
;      by the value just loaded. The second cannot begin until the first retires. Four characters
;      is four such chains back to back, and at this length there is nothing else in flight to
;      hide them.
;
; This variant changes the SHAPE of the short path, not the arithmetic:
;
;   * It decides "short and case-insensitive" using only volatile registers, so that path touches
;     no callee-saved register, needs no `vzeroupper`, and spends one push instead of five.
;
;   * It compares the two RAW wchars first and consults the table only when they DIFFER. If two
;     characters are bit-identical their upcased forms are identical -- the table is a function, so
;     upcase(x) == upcase(x) holds with no assumption about what the table contains. Every equal
;     position therefore costs two independent loads and a compare, and the dependent chain is paid
;     only at a position that actually differs, which for a compare that is not an immediate
;     mismatch is at most one position in the whole call.
;
; The FOLDING ITSELF IS UNCHANGED: a differing pair is still resolved through `wia_upcase`, the same
; table the parent uses, built once from the OS so the result is bit-exact rather than an ASCII
; approximation. This matters -- a variant that folded arithmetically would be fast and wrong above
; U+007F, and correctness is not the axis being traded here.
;
; The parent is left untouched: its numbers were taken on a machine with different push/pop and
; AVX-transition costs, and editing it would re-attribute them.
;
; ISA: AVX2 + BMI1, same as the parent. Validated on bench #3 (Intel i9-11900H, Tiger Lake-H) --
; see docs/PLATFORM-i9-11900H.md.
;
; UNICODE_STRING: Length @+0 (u16 bytes), Buffer @+8.

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
        ; Nothing here may touch rbx/rsi/rdi/r12/r13: the whole point is that the short CI path
        ; reaches its answer without ever having saved them.
        movzx     r9d, word ptr [rcx]              ; Length1 (bytes)
        movzx     r10d, word ptr [rdx]             ; Length2 (bytes)
        mov       r11, [rcx + 8]                   ; Buffer1
        mov       rcx, [rdx + 8]                   ; Buffer2   (rcx is free from here)
        mov       eax, r9d
        sub       eax, r10d                        ; lenDiff = L1 - L2, the prefix-equal answer
        cmp       r9d, r10d
        cmovg     r9d, r10d                        ; r9d = common = min(L1,L2) bytes

        test      r8b, r8b
        jz        heavy                            ; case-sensitive -> the parent's path
        cmp       r9d, 32
        jae       heavy                            ; long enough to vectorize -> the parent's path

; ---------------- short, case-insensitive: the only path this variant changes ----------------
; Live here: r11 = Buffer1, rcx = Buffer2, r9d = common bytes, eax = lenDiff, rdx = offset,
; r8d/r10d = scratch, rbx = the upcase table base (the one register worth saving).
        push      rbx
        lea       rbx, wia_upcase
        xor       edx, edx
cis_loop:
        cmp       edx, r9d
        jae       cis_done                         ; common exhausted; eax already holds lenDiff
        movzx     r8d, word ptr [r11 + rdx]
        movzx     r10d, word ptr [rcx + rdx]
        cmp       r8d, r10d
        je        cis_next                         ; identical raw wchars fold identically
        ; They differ, so and only so does the fold decide the answer. Two dependent loads, once.
        movzx     r8d, word ptr [rbx + r8*2]
        movzx     r10d, word ptr [rbx + r10*2]
        cmp       r8d, r10d
        jne       cis_sub
cis_next:
        add       edx, 2
        jmp       cis_loop
cis_sub:
        mov       eax, r8d
        sub       eax, r10d                        ; sign is the contract; magnitude is unspecified
cis_done:
        pop       rbx
        ret                                        ; no vzeroupper: no ymm was ever written

; ---------------- everything below is the parent's, reached via this shim --------------------
; The parent's body expects its own register assignment, so it is restored here rather than
; rewritten -- the case-sensitive scan and the CI vector path are not what this variant is about.
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
