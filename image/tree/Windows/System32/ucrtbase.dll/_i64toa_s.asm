; ucrtbase.dll!_i64toa_s  --  hand-written x86-64 reimplementation (2.32x vs shipped)
; source of truth: changes/194-i64toa-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/194-i64toa-s/impl.asm
; errno_t wia_i64toa_s(__int64 Value, char* Buffer, size_t SizeInChars, int Radix)
;   [rcx, rdx, r8, r9d -> eax]
;
; Reimplements ucrtbase!_i64toa_s; the bounded form of change 057. ucrtbase's costs 38.1 ns for a
; 19-digit value, because its inner loop issues a 64-bit `div rax, rdi` PER DIGIT.
;
; The contract's error path could not be fitted black-box: the partial content left in the caller's
; buffer on ERANGE followed no rule that also explained the size-2 negative case, where nothing but
; buffer[0] is touched. The shipped code settled it (dumpbin /disasm ucrtbase.dll, RVA 0x00079D60
; and its shared worker at 0x00076F10):
;
;   * Buffer == NULL or SizeInChars == 0 -> errno = EINVAL (22), handler, return 22,
;     and nothing is written, not even Buffer[0].
;   * Otherwise BUFFER[0] = 0 Is written immediately, before any other validation. That is why an
;     invalid radix still empties the buffer while size 0 leaves it untouched.
;   * negative := (Radix == 10 && Value < 0). For every other radix the 64-bit value is formatted
;     UNSIGNED, _i64toa_s(-1, ..., 16) gives "ffffffffffffffff", exactly as change 057 records.
;   * If SizeInChars <= negative + 1 -> Erange (34) Immediately, with Buffer[0] = 0 and nothing
;     else touched. This is the case no fitted rule reproduced: for a negative value and size 2 it
;     fires BEFORE a single digit is emitted.
;   * Radix outside 2..36 -> EINVAL (22), Buffer[0] = 0.
;   * Otherwise digits are emitted LEAST-SIGNIFICANT FIRST straight into the caller's buffer, and
;     only reversed in place at the end. So on ERANGE the caller is left with however many REVERSED
;     digits fitted, then Buffer[0] = 0:
;         1234 size 4 -> 0 '3' '2' '1'      (all four cells used, no room for the terminator)
;         -1234 size 3 -> 0 '4' '3'         ('-' at [0] first, two digits, then [0] = 0)
;   * On success the terminator fits and the result is the ordinary string. errno is set BEFORE the
;     handler is invoked on both error paths.
;
; Method: digits are generated into a stack scratch, FORWARD, using change 057's 2-digit decimal
; table for radix 10 and a shift for radix 16, no `div` at all on the two common radixes, against
; one 64-bit division per digit in the shipped version. Only then is the fit decided, and the
; scratch is copied forward (success) or BACKWARD (the ERANGE partial), which is what reproduces
; ucrtbase's reversed leftovers exactly without emitting into the caller's buffer twice.
;
; The 2-digit table is assembled here rather than built at run time (changes 054-057 call
; wia_dec2b_init first), so this function is self-contained and can be hot-patched over the live
; export with no initialisation step.
;
; ISA: baseline x64. No AVX needed; the work is 20 digits, not 20 kilobytes.

EXTERN _errno:PROC
EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
dec2b   db "00010203040506070809"
        db "10111213141516171819"
        db "20212223242526272829"
        db "30313233343536373839"
        db "40414243444546474849"
        db "50515253545556575859"
        db "60616263646566676869"
        db "70717273747576777879"
        db "80818283848586878889"
        db "90919293949596979899"

.code
wia_i64toa_s PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        sub       rsp, 68h                     ; 32 shadow + 72 scratch; rsp ends 16-aligned
        mov       rbx, rdx                     ; Buffer
        mov       rsi, r8                      ; SizeInChars
        mov       r10d, r9d                    ; Radix

        test      rbx, rbx
        jz        e_inval_none
        test      rsi, rsi
        jz        e_inval_none                 ; nothing is written in either case

        mov       byte ptr [rbx], 0            ; written BEFORE the remaining validation

        xor       r12d, r12d                   ; negative
        cmp       r10d, 10
        jne       nn
        test      rcx, rcx
        jns       nn
        mov       r12d, 1
        neg       rcx                          ; magnitude; _I64_MIN negates to 2^63 unsigned
nn:
        lea       rax, [r12 + 1]
        cmp       rsi, rax
        jbe       e_range                      ; size <= negative+1 -> ERANGE before any digit
        mov       eax, r10d
        sub       eax, 2
        cmp       eax, 34
        ja        e_inval_wrote                ; radix outside 2..36

        ;================ emit the digits FORWARD into the scratch ================
        lea       rdi, [rsp + 68h]             ; one past the scratch; every store pre-decrements
        mov       rax, rcx                     ; magnitude, treated as unsigned
        cmp       r10d, 10
        je        d10
        ; Every power-of-two radix (2, 4, 8, 16, 32) can shift instead of divide. Only radix 16
        ; was special-cased at first, which left radix 2 issuing 64 divisions and measuring a 1.00x
        ; tie; with the shift it becomes one of the widest wins here. The test is done in r9d, not
        ; eax, because eax already holds the magnitude.
        lea       r9d, [r10 - 1]
        test      r9d, r10d
        jnz       dgen
        mov       r11d, r9d                    ; mask = radix-1
        bsf       ecx, r10d                    ; shift = log2(radix)
        jmp       dpow

dgen:                                          ; the rare radixes keep a division, like ucrtbase
        mov       ecx, r10d
dgen_l:
        xor       rdx, rdx
        div       rcx
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        dgen_s
        lea       r9d, [rdx + 57h]             ; lowercase for values above 9
dgen_s:
        dec       rdi
        mov       byte ptr [rdi], r9b
        test      rax, rax
        jnz       dgen_l
        jmp       emitted

d10:
        lea       r8, dec2b
        mov       ecx, 100
d10_l:
        cmp       rax, 100
        jb        d10_last
        xor       rdx, rdx
        div       rcx
        sub       rdi, 2
        mov       r9w, word ptr [r8 + rdx*2]   ; two decimal digits at a time
        mov       word ptr [rdi], r9w
        jmp       d10_l
d10_last:
        cmp       eax, 10
        jb        d10_one
        sub       rdi, 2
        mov       r9w, word ptr [r8 + rax*2]
        mov       word ptr [rdi], r9w
        jmp       emitted
d10_one:
        dec       rdi
        add       eax, 30h
        mov       byte ptr [rdi], al
        jmp       emitted

dpow:                                          ; radix is a power of two: shift, never divide
        mov       edx, eax
        and       edx, r11d
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        dpow_s
        lea       r9d, [rdx + 57h]
dpow_s:
        dec       rdi
        mov       byte ptr [rdi], r9b
        shr       rax, cl
        jnz       dpow

emitted:
        lea       r8, [rsp + 68h]              ; one past the last (least significant) digit
        mov       rcx, r8
        sub       rcx, rdi                     ; rcx = digit count

        ;================ does it fit? negative + digits + terminator ================
        lea       rax, [r12 + rcx + 1]
        cmp       rax, rsi
        ja        partial

        mov       rdx, rbx
        test      r12d, r12d
        jz        s_nosign
        mov       byte ptr [rdx], 2Dh          ; '-'
        inc       rdx
s_nosign:
        ; Copy the digits 8 bytes at a time. A byte-at-a-time loop cost ~13 cycles on a 13-digit
        ; base-36 value and put that class at 0.92x; a regression. Every wide store here is
        ; provably inside the buffer: success means negative + digits + 1 <= SizeInChars, so while
        ; 8 or more digits remain there are at least 9 cells left.
        mov       rax, rcx                     ; digit count
        cmp       rax, 8
        jb        s_small
s_l8:
        mov       r9, qword ptr [rdi]
        mov       qword ptr [rdx], r9
        add       rdi, 8
        add       rdx, 8
        sub       rax, 8
        cmp       rax, 8
        jae       s_l8
        test      rax, rax
        jz        s_done
        lea       rdi, [rdi + rax - 8]         ; overlapping final 8, still inside the digit run
        lea       rdx, [rdx + rax - 8]
        mov       r9, qword ptr [rdi]
        mov       qword ptr [rdx], r9
        lea       rdx, [rdx + 8]
        jmp       s_done
s_small:
        test      rax, rax
        jz        s_done
s_sb:
        mov       r9b, byte ptr [rdi]
        mov       byte ptr [rdx], r9b
        inc       rdi
        inc       rdx
        dec       rax
        jnz       s_sb
s_done:
        mov       byte ptr [rdx], 0
        xor       eax, eax
        jmp       epi

        ;================ ERANGE: leave exactly ucrtbase's reversed leftovers ================
partial:
        mov       rax, rsi
        sub       rax, r12                     ; cells available after the sign
        cmp       rax, rcx
        cmova     rax, rcx                     ; k = min(available, digits)
        lea       rdx, [rbx + r12]             ; the sign cell is overwritten by the 0 below
        lea       r9, [r8 - 1]                 ; the least significant digit
p_cp:
        test      rax, rax
        jz        p_done
        movzx     r11d, byte ptr [r9]
        mov       byte ptr [rdx], r11b
        dec       r9
        inc       rdx
        dec       rax
        jmp       p_cp
p_done:
        mov       byte ptr [rbx], 0
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
        call      _errno                       ; ucrtbase's own, so the caller sees it where it
        mov       dword ptr [rax], r12d        ; expects; errno is set BEFORE the handler, as in
        call      _invalid_parameter_noinfo    ; the shipped code
        mov       eax, r12d
epi:
        add       rsp, 68h
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_i64toa_s ENDP
END
