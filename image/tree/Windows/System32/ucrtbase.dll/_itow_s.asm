; ucrtbase.dll!_itow_s  --  hand-written x86-64 reimplementation (1.65x vs shipped)
; source of truth: changes/200-itow-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/200-itow-s/impl.asm
; errno_t wia_itow_s(int Value, wchar_t* Buffer, size_t SizeInChars, int Radix)
;   [rcx, rdx, r8, r9d -> eax]
;
; Reimplements ucrtbase!_itow_s. `_ltow_s` is a SEPARATE export at a different address (0x000669E0
; against 0x00064FB0) with the same behaviour; one implementation covers both names and the harness
; checks both exports rather than assuming it.
;
; Bounded form of change 074, and the wide form of change 198.
;
; The 32-BIT family is the same machine, half the width. ucrtbase lays it out identically to the
; 64-bit one: the signed entry computes `negative = (Radix == 10 && Value < 0)` and calls a shared
; worker (0x0003588C, mirroring 0x00079DAC), the unsigned entry passes a hard zero to the same
; address, and the worker tail-jumps to a digit emitter (0x000654D0, mirroring 0x00076F10) that
; emits least-significant-first and reverses in place. Instruction for instruction the two workers
; are the same code with `mov r10d, ecx` where the 64-bit one has `mov r10, rcx`, and `div eax, edi`
; where it has `div rax, rdi`.
;
; That one difference is the whole contract difference: the magnitude is 32 bits, so for any radix
; other than 10 the value is formatted as an UNSIGNED 32-BIT quantity. _itoa_s(-1, buf, n, 16)
; gives "ffffffff", eight f's, not the sixteen that change 194 produces.
;
; Everything else is change 194's contract, read out of the shipped disassembly because the ERANGE
; path could not be fitted from probing:
;   * Buffer == NULL or SizeInChars == 0 -> EINVAL (22), nothing written;
;   * otherwise Buffer[0] = 0 is written IMMEDIATELY, before the rest of the validation;
;   * SizeInChars <= negative + 1 -> ERANGE (34) before a single digit is emitted;
;   * Radix outside 2..36 -> EINVAL (22), Buffer[0] = 0;
;   * a buffer that runs out keeps exactly the REVERSED prefix it held, then Buffer[0] = 0;
;   * errno is set BEFORE the invalid-parameter handler on both error paths.
;
; SizeInChars counts CHARACTERS. The 2-digit decimal table holds a DWORD per entry, so radix 10
; still writes two digits per store and still divides only once per two digits.
;
; ISA: baseline x64.

EXTERN _errno:PROC
EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
; Maximum digits a 32-bit unsigned value needs, indexed by radix. One compare against
; this decides, BEFORE the emit begins, whether the buffer could possibly be too small;
; if it cannot be, the fast loops below run with no bound check at all.
maxdig  db 0,0,32,21,16,14,13,12,11,11,10,10,9,9,9,9,8,8,8,8,8,8,8,8,7,7,7,7,7,7,7,7,7,7,7,7,7

; "00" "01" ... "99", each pair as two wide characters

dec2w   dw  30h,30h, 30h,31h, 30h,32h, 30h,33h, 30h,34h, 30h,35h, 30h,36h, 30h,37h, 30h,38h, 30h,39h
        dw  31h,30h, 31h,31h, 31h,32h, 31h,33h, 31h,34h, 31h,35h, 31h,36h, 31h,37h, 31h,38h, 31h,39h
        dw  32h,30h, 32h,31h, 32h,32h, 32h,33h, 32h,34h, 32h,35h, 32h,36h, 32h,37h, 32h,38h, 32h,39h
        dw  33h,30h, 33h,31h, 33h,32h, 33h,33h, 33h,34h, 33h,35h, 33h,36h, 33h,37h, 33h,38h, 33h,39h
        dw  34h,30h, 34h,31h, 34h,32h, 34h,33h, 34h,34h, 34h,35h, 34h,36h, 34h,37h, 34h,38h, 34h,39h
        dw  35h,30h, 35h,31h, 35h,32h, 35h,33h, 35h,34h, 35h,35h, 35h,36h, 35h,37h, 35h,38h, 35h,39h
        dw  36h,30h, 36h,31h, 36h,32h, 36h,33h, 36h,34h, 36h,35h, 36h,36h, 36h,37h, 36h,38h, 36h,39h
        dw  37h,30h, 37h,31h, 37h,32h, 37h,33h, 37h,34h, 37h,35h, 37h,36h, 37h,37h, 37h,38h, 37h,39h
        dw  38h,30h, 38h,31h, 38h,32h, 38h,33h, 38h,34h, 38h,35h, 38h,36h, 38h,37h, 38h,38h, 38h,39h
        dw  39h,30h, 39h,31h, 39h,32h, 39h,33h, 39h,34h, 39h,35h, 39h,36h, 39h,37h, 39h,38h, 39h,39h

.code
wia_itow_s PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        sub       rsp, 0A8h                    ; 32 shadow + 136 scratch; rsp ends 16-aligned
        mov       rbx, rdx                     ; Buffer
        mov       rsi, r8                      ; SizeInChars (characters, not bytes)
        mov       r10d, r9d                    ; Radix

        test      rbx, rbx
        jz        e_inval_none
        test      rsi, rsi
        jz        e_inval_none

        mov       word ptr [rbx], 0            ; written BEFORE the remaining validation

        xor       r12d, r12d                   ; negative
        cmp       r10d, 10
        jne       nn
        test      ecx, ecx
        jns       nn
        mov       r12d, 1
        neg       ecx                          ; INT_MIN negates to 2^31 unsigned
nn:
        lea       rax, [r12 + 1]
        cmp       rsi, rax
        jbe       e_range                      ; size <= negative+1 -> ERANGE before any digit
        mov       eax, r10d
        sub       eax, 2
        cmp       eax, 34
        ja        e_inval_wrote

        ;================ emit the digits FORWARD into the scratch ================
        ; Can the buffer even be too small? Decide here, once, instead of bound-checking every
        ; digit. ucrtbase emits straight into the caller's buffer and stops when full; generating
        ; every digit and only then finding it does not fit made the 10-digit-into-6-cells case
        ; measure 0.93x (a regression) because a 32-bit `div` is cheap enough for the extra
        ; digits to show. But putting the bound check INSIDE the loops cost ~1.5 cycles per digit
        ; and was worse still: base 36 fell to 0.88x and base 2 from 3.98x to 2.51x.
        ; So the check is hoisted. If the cells available cannot hold the widest possible result
        ; for this radix, the bounded loop below runs; otherwise the fast loops run untouched.
        mov       r11, rsi
        sub       r11, r12                     ; cells available for digits = size - negative
        lea       rax, maxdig
        movzx     eax, byte ptr [rax + r10]    ; widest result this radix can produce
        lea       rdi, [rsp + 0A8h]              ; one past the scratch; stores pre-decrement
        cmp       r11, rax
        ja        fits_for_sure                ; cells > widest result => digits + terminator fit
        ; ---- bounded emit: only reachable when the buffer really might be too small, so it is
        ; ---- kept simple, one generic division per digit, every radix, no table, no shift.
        add       r11, r11                     ; ... in bytes
        mov       rax, rdi
        sub       rax, r11
        mov       r11, rax                     ; r11 = the lowest address the digits may reach
        mov       eax, ecx                     ; magnitude
        mov       ecx, r10d
        lea       r8, [rbx + r12*2]              ; ...and a FORWARD cursor into the caller's buffer.
        ; Each digit is stored TWICE: descending into the scratch (so the fitted case can copy it
        ; out most-significant-first) and ascending into the caller's buffer. The second store is
        ; free; this loop is bound by a 32-bit `div` at ~15 cycles, which leaves the store ports
        ; idle, and it is exactly the cell ucrtbase would have left there, because ucrtbase emits
        ; into the caller's buffer least-significant-first and simply stops when it runs out. So
        ; the ERANGE exit below has nothing left to do but write the terminator.
        ; This is what took the ERANGE class from below 1.00x to a win: the old code generated into
        ; the scratch and then walked it back out one cell at a time, and that second dependent
        ; loop was the entire deficit against ucrtbase.
        ; Radix 10 Gets its own bounded loop. This path is the erange case, and erange is
        ; dominated by two calls into ucrtbase (_errno and _invalid_parameter_noinfo) that our
        ; contract obliges us to make and that ucrtbase pays too, so the only part of the class
        ; we can actually win is the digit loop, and at the generic `div` it was an exact tie.
        ; Radix 10 is a compile-time constant here, so the divide becomes a constant reciprocal:
        ; v/10 == (v * 0CCCCCCCDh) >> 35 for every 32-bit v (verified exhaustively near both ends
        ; of the range and over 500k random values). imul+shr is a ~4-cycle loop-carried chain
        ; against the divider's ~10-12.
        cmp       r10d, 10
        jne       ce_l
        mov       r9d, 0CCCCCCCDh              ; zero-extends; the immediate form would sign-extend
c10_l:
        mov       edx, eax
        imul      rdx, r9                      ; v * M   (< 2^64: 0FFFFFFFFh * 0CCCCCCCDh = 1.48e19)
        shr       rdx, 35                       ; q = v / 10
        lea       ecx, [rdx + rdx*4]
        add       ecx, ecx                      ; q * 10
        sub       eax, ecx                      ; rem = v - q*10, 0..9
        add       eax, 30h                      ; ...which for radix 10 is always a plain digit
        sub       rdi, 2
        mov       word ptr [rdi], ax
        mov       word ptr [r8], ax
        add       r8, 2
        mov       eax, edx                      ; v = q
        test      eax, eax
        jz        emitted                       ; it fitted after all
        cmp       rdi, r11
        ja        c10_l
        jmp       capped

ce_l:
        xor       edx, edx
        div       ecx
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        ce_s
        lea       r9d, [rdx + 57h]
ce_s:
        sub       rdi, 2
        mov       word ptr [rdi], r9w
        mov       word ptr [r8], r9w           ; the reversed prefix, written as it is generated
        add       r8, 2
        test      eax, eax
        jz        emitted                      ; it fitted after all
        cmp       rdi, r11
        ja        ce_l
        jmp       capped

fits_for_sure:
        lea       rdi, [rsp + 0A8h]            ; one past the scratch; every store pre-decrements
        mov       eax, ecx                     ; magnitude; the 32-bit mov zero-extends, which
                                               ; is exactly the unsigned 32-bit value a radix
                                               ; other than 10 must format
        cmp       r10d, 10
        je        d10
        lea       r9d, [r10 - 1]               ; power-of-two radixes shift instead of dividing
        test      r9d, r10d
        jnz       dgen
        mov       r11d, r9d                    ; mask = radix-1
        bsf       ecx, r10d                    ; shift = log2(radix)
        jmp       dpow

dgen:
        ; Measured, not assumed: a reciprocal multiply was tried here and is not faster on this
        ; core. Two forms were built and benchmarked against this divide, both bit-exact:
        ;   * magic scaled to 2^38, quotient extracted with `shrd rax, rdx, 38`, base 36 went
        ;     8.19 ns -> 10.45 ns (0.99x -> 0.77x); shrd-with-immediate is multi-uop here;
        ;   * magic scaled to 2^64, quotient arriving in rdx with no shift at all, 8.45 ns, still
        ;     short of the divide.
        ; Zen 4's 32-bit divider is simply fast enough that a dependent `mul` chain does not beat
        ; it at these digit counts, and the divide keeps the loop four instructions shorter.
        mov       ecx, r10d
dgen_l:
        xor       edx, edx
        div       ecx
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        dgen_s
        lea       r9d, [rdx + 57h]
dgen_s:
        sub       rdi, 2
        mov       word ptr [rdi], r9w
        test      eax, eax
        jnz       dgen_l
        jmp       emitted

d10:
        lea       r8, dec2w
        mov       ecx, 100
d10_l:
        cmp       eax, 100
        jb        d10_last
        xor       edx, edx
        div       ecx
        sub       rdi, 4
        mov       r9d, dword ptr [r8 + rdx*4]  ; two wide digits in one dword
        mov       dword ptr [rdi], r9d
        jmp       d10_l
d10_last:
        cmp       eax, 10
        jb        d10_one
        sub       rdi, 4
        mov       r9d, dword ptr [r8 + rax*4]
        mov       dword ptr [rdi], r9d
        jmp       emitted
d10_one:
        sub       rdi, 2
        add       eax, 30h
        mov       word ptr [rdi], ax
        jmp       emitted

dpow:
        mov       edx, eax
        and       edx, r11d
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        dpow_s
        lea       r9d, [rdx + 57h]
dpow_s:
        sub       rdi, 2
        mov       word ptr [rdi], r9w
        shr       eax, cl
        jnz       dpow

emitted:
        lea       r8, [rsp + 0A8h]             ; one past the least significant digit
        mov       rcx, r8
        sub       rcx, rdi
        shr       rcx, 1                       ; rcx = digit count, in CHARACTERS

        ;================ does it fit? negative + digits + terminator ================
        lea       rax, [r12 + rcx + 1]
        cmp       rax, rsi
        ja        partial

        mov       rdx, rbx
        test      r12d, r12d
        jz        s_nosign
        mov       word ptr [rdx], 2Dh          ; '-'
        add       rdx, 2
s_nosign:
        ; Copy four characters (8 bytes) at a time. Success means negative + digits + 1 <=
        ; SizeInChars, so while four or more characters remain there are at least five cells left
        ; and an 8-byte store cannot pass the buffer.
        mov       rax, rcx                     ; characters remaining
        cmp       rax, 4
        jb        s_small
s_l4:
        mov       r9, qword ptr [rdi]
        mov       qword ptr [rdx], r9
        add       rdi, 8
        add       rdx, 8
        sub       rax, 4
        cmp       rax, 4
        jae       s_l4
        test      rax, rax
        jz        s_done
        lea       rdi, [rdi + rax*2 - 8]       ; overlapping final four, still inside the run
        lea       rdx, [rdx + rax*2 - 8]
        mov       r9, qword ptr [rdi]
        mov       qword ptr [rdx], r9
        lea       rdx, [rdx + 8]
        jmp       s_done
s_small:
        test      rax, rax
        jz        s_done
        ; 4..7 characters in TWO overlapping 8-byte moves instead of up to seven dependent word
        ; iterations. This is the base-36 class: a 32-bit value is at most 7 digits there, so it
        ; always landed in the word loop, and that loop was the whole remaining deficit against
        ; ucrtbase, which reverses in place and never copies at all. Both moves stay strictly
        ; inside the digit run at both ends, so nothing outside [rdx, rdx+2*count) is written.
        cmp       rax, 4
        jae       s_wide
        ; 1..3 characters, BRANCH-FREE: first cell, last cell, middle cell. For count 1 all three
        ; target the same cell; for 2 they cover 0 and 1; for 3 they cover 0, 2 and 1. This
        ; replaces a counted loop whose per-iteration overhead dominated the two-digit case, the
        ; smallest class in the bench, and the one where ucrtbase was still ahead.
        mov       r9w, word ptr [rdi]
        mov       word ptr [rdx], r9w
        mov       r9w, word ptr [rdi + rax*2 - 2]
        mov       word ptr [rdx + rax*2 - 2], r9w
        mov       r11, rax
        shr       r11, 1
        mov       r9w, word ptr [rdi + r11*2]
        mov       word ptr [rdx + r11*2], r9w
        lea       rdx, [rdx + rax*2]
        jmp       s_done
s_wide:
        mov       r9, qword ptr [rdi]
        mov       qword ptr [rdx], r9
        mov       r9, qword ptr [rdi + rax*2 - 8]
        mov       qword ptr [rdx + rax*2 - 8], r9
        lea       rdx, [rdx + rax*2]
        jmp       s_done
s_done:
        mov       word ptr [rdx], 0
        xor       eax, eax
        jmp       epi

        ;================ the emit stopped early: the result cannot fit ================
capped:
        ; The bounded loop already wrote the reversed prefix into the caller's buffer as it went,
        ; and it stopped exactly at the cell limit, so the leftover ucrtbase would have produced is
        ; already in place. Only the terminator is missing. (`partial` below stays as the general
        ; path: it still serves the case where the bounded loop consumed the value on its last
        ; permitted digit, and it does not depend on maxdig being exact.)
        mov       word ptr [rbx], 0
        jmp       e_range

        ;================ ERANGE: leave exactly ucrtbase's reversed leftovers ================
partial:
        mov       rax, rsi
        sub       rax, r12                     ; cells available after the sign
        cmp       rax, rcx
        cmova     rax, rcx                     ; k = min(available, digits)
        lea       rdx, [rbx + r12*2]           ; the sign cell is overwritten by the 0 below
        lea       r9, [r8 - 2]                 ; the least significant digit
p_cp:
        test      rax, rax
        jz        p_done
        movzx     r11d, word ptr [r9]
        mov       word ptr [rdx], r11w
        sub       r9, 2
        add       rdx, 2
        dec       rax
        jmp       p_cp
p_done:
        mov       word ptr [rbx], 0
        jmp       e_range

e_inval_none:
        mov       r12d, 22
        jmp       err_common
e_inval_wrote:
        mov       r12d, 22
        jmp       err_common
e_range:
        mov       r12d, 34
err_common:
        call      _errno
        mov       dword ptr [rax], r12d        ; errno BEFORE the handler, as in the shipped code
        call      _invalid_parameter_noinfo
        mov       eax, r12d
epi:
        add       rsp, 0A8h
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_itow_s ENDP
END
