; ntdll.dll!RtlUdiv128  --  hand-written x86-64 reimplementation (6.66x vs shipped)
; source of truth: changes/204-rtludiv128/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/204-rtludiv128/impl.asm
; ULONG64 wia_udiv128(ULONG64 DividendHigh, ULONG64 DividendLow, ULONG64 Divisor, ULONG64* Remainder)
;   [rcx, rdx, r8, r9 -> rax]
;
; Reimplements ntdll!RtlUdiv128, which costs a FLAT ~69 ns for one 128/64 division whatever the
; operands. The shipped code at RVA 0x0014A250 says why: a 64-iteration restoring shift-subtract
; long division, branch-free inside the loop but always sixty-four passes.
;
; x86-64 already has this instruction. `div r64` divides rdx:rax by a 64-bit operand in ~20-40
; cycles. The only reason a software loop exists is that `div` raises #DE when the quotient will not
; fit in 64 bits, and that is exactly the condition
;
;       quotient >= 2^64   <=>   hi:lo >= Divisor * 2^64   <=>   DividendHigh >= Divisor
;
; so one unsigned compare separates the two regions with no slack at all.
;
; Why the overflow region is not folded into a formula. Two closed forms were tried and both are
; wrong, and both survived a badly-chosen corpus long enough to be believed:
;
;   * "the quotient saturates to all-ones, remainder = DividendLow + Divisor". True for
;     (hi=1, lo=0, d=1) -> FFFFFFFFFFFFFFFF r 1, and for every case the first probe happened to
;     pick. False for (hi=7FFFFFFFFFFFFFFF, lo=0, d=100000000h), where the live export returns
;     FFFFFFFF00000000 r 0.
;   * "the true quotient reduced mod 2^64". That explains the second case and contradicts the first,
;     where mod 2^64 would give 0 and the export gives all-ones.
;
; Neither holds because the loop's 64-bit remainder register OVERFLOWS once the quotient needs more
; than 64 bits: the `sar 63` trick recovers one lost bit for the comparison, but the bits already
; shifted out of the top of r are simply gone. The result is deterministic and reproducible, and has
; no closed form. So this implementation reproduces the loop there, instruction for instruction --
; matching ntdll's speed on a degenerate input rather than guessing at a rule.
;
; That is not a compromise on the useful case: hi < Divisor is the only region where a 128/64
; quotient is representable at all, and it gets a single hardware divide.
;
; Divisor == 0 needs no case of its own, DividendHigh >= 0 is always true, so it takes the loop
; and never reaches a `div`. The shipped function does not fault on a zero divisor and neither may
; this one; the loop returns all-ones with Remainder = DividendLow.
;
; The Remainder pointer may be NULL; the shipped code tests it before storing, and so does this.
;
; ISA: baseline x64.

.code
wia_udiv128 PROC
        cmp       rcx, r8                      ; DividendHigh vs Divisor, unsigned
        jae       slow                         ; >= : quotient will not fit. d == 0 lands here too.

        ;================ the quotient fits: ONE hardware division ================
        mov       rax, rdx                     ; low half of the dividend
        mov       rdx, rcx                     ; high half
        div       r8                           ; rax = quotient, rdx = remainder. Cannot #DE:
                                               ;   hi < d is exactly the no-overflow condition,
                                               ;   and hi < d forces d >= 1.
        test      r9, r9
        jz        fast_done
        mov       qword ptr [r9], rdx
fast_done:
        ret

        ;================ the degenerate region: the shipped loop, reproduced ================
        ; rbx and rdi are callee-saved and are spilled HERE rather than in the prologue, so the fast
        ; path above does not pay for two pushes and two pops it never needs.
slow:
        push      rbx
        push      rdi
        mov       r10, rcx                     ; r = DividendHigh
        mov       rbx, rdx                     ; q = DividendLow
        mov       edi, 64
slow_loop:
        lea       rax, [r10 + r10]             ; r << 1
        mov       r11, rbx
        shr       r11, 63                      ; the top bit of q
        lea       rcx, [rbx + rbx]             ; q << 1
        or        r11, rax                     ; candidate remainder
        mov       rdx, r10
        sar       rdx, 63                      ; all ones if a bit left the top of r
        mov       r10, r11
        sub       r10, r8                      ; candidate - Divisor
        mov       rax, r11
        or        rax, rdx                      ; saturate the test value, so an iteration that
                                               ;   overflowed always subtracts
        mov       rbx, rcx
        or        rbx, 1                        ; q with this quotient bit set
        cmp       rax, r8
        cmovb     r10, r11                      ; below: restore the remainder...
        cmovb     rbx, rcx                      ; ...and leave the quotient bit clear
        sub       rdi, 1
        jne       slow_loop

        test      r9, r9
        jz        slow_done
        mov       qword ptr [r9], r10
slow_done:
        mov       rax, rbx
        pop       rdi
        pop       rbx
        ret
wia_udiv128 ENDP
END
