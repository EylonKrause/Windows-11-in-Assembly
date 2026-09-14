; changes/195-ui64toa-s/impl.asm
; errno_t wia_ui64toa_s(unsigned __int64 Value, char* Buffer, size_t SizeInChars, int Radix)
;   [rcx, rdx, r8, r9d -> eax]
;
; Reimplements ucrtbase!_ui64toa_s -- the unsigned sibling of change 194, and the bounded form of
; change 055. 41.4 ns for a 20-digit value in the shipped version, again a 64-bit `div` per digit.
;
; The two share ONE worker in ucrtbase: _i64toa_s at RVA 0x00079D60 computes
; `negative = (Radix == 10 && Value < 0)` and calls 0x00079DAC; _ui64toa_s at 0x00079D90 passes a
; hard zero and calls the same address. So this is change 194 with `negative` permanently 0, and
; every contract detail 194 records applies unchanged:
;
;   * Buffer == NULL or SizeInChars == 0 -> EINVAL (22), NOTHING written;
;   * otherwise Buffer[0] = 0 is written IMMEDIATELY, before the rest of the validation;
;   * SizeInChars <= 1 -> ERANGE (34) before a digit is emitted (the `negative + 1` test with
;     negative fixed at 0);
;   * Radix outside 2..36 -> EINVAL (22), Buffer[0] = 0;
;   * digits are emitted least-significant-first and reversed in place, so a buffer that runs out
;     keeps exactly the REVERSED prefix it held, then Buffer[0] = 0;
;   * errno is set BEFORE the invalid-parameter handler on both error paths.
;
; Method is 194's: digits into a stack scratch first so the fit is decided before the caller's
; buffer is touched, then copied forward (success) or backward (the ERANGE partial). Radix 10 uses
; the assembled 2-digit table, every power-of-two radix shifts, the rest divide.
;
; ISA: baseline x64.

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
wia_ui64toa_s PROC
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

        xor       r12d, r12d                   ; negative is permanently 0 for the unsigned form,
                                               ; which is exactly what ucrtbase's _ui64toa_s passes
                                               ; to the worker that _i64toa_s shares with it
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
        ; Every power-of-two radix -- 2, 4, 8, 16, 32 -- can shift instead of divide. Only radix 16
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
        ; base-36 value and put that class at 0.92x -- a regression. Every wide store here is
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
wia_ui64toa_s ENDP
END
