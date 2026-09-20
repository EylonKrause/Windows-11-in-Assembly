; changes/196-i64tow-s/impl.asm
; errno_t wia_i64tow_s(__int64 Value, wchar_t* Buffer, size_t SizeInChars, int Radix)
;   [rcx, rdx, r8, r9d -> eax]
;
; Reimplements ucrtbase!_i64tow_s -- the wide form of change 194 and the bounded form of change
; 075. 37.8 ns for a 19-digit value in the shipped version, one 64-bit `div` per digit.
;
; The contract is change 194's, and that is a MEASUREMENT rather than an inheritance: the probe in
; ../194-i64toa-s/probes/its.c ran the byte and wide forms side by side over 200 000 random
; (value, size, radix) triples and compared them character for character, including the untouched
; cells past the terminator -- 0 differences. So every rule change 194 read out of the shipped
; disassembly holds here:
;
;   * Buffer == NULL or SizeInChars == 0 -> EINVAL (22), nothing written;
;   * otherwise Buffer[0] = 0 is written IMMEDIATELY, before the rest of the validation;
;   * SizeInChars <= negative + 1 -> ERANGE (34) before a single digit is emitted;
;   * Radix outside 2..36 -> EINVAL (22), Buffer[0] = 0;
;   * digits go in least-significant-first and are reversed in place, so a buffer that runs out
;     keeps exactly the REVERSED prefix it held, then Buffer[0] = 0;
;   * negative := (Radix == 10 && Value < 0); every other radix formats the value UNSIGNED;
;   * errno is set BEFORE the invalid-parameter handler on both error paths.
;
; Everything that is a byte in change 194 is a 16-bit unit here, which changes two things beyond
; the obvious store width:
;   * the 2-digit decimal table holds a DWORD per entry (two wide characters), so radix 10 still
;     writes two digits per store and still divides only once per two digits;
;   * SizeInChars counts CHARACTERS, so the digit count, the fit test and both copy loops work in
;     characters while the wide copy still moves 8 bytes -- four characters -- at a time.
;
; ISA: baseline x64.

EXTERN _errno:PROC
EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
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
wia_i64tow_s PROC
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
        test      rcx, rcx
        jns       nn
        mov       r12d, 1
        neg       rcx
nn:
        lea       rax, [r12 + 1]
        cmp       rsi, rax
        jbe       e_range                      ; size <= negative+1 -> ERANGE before any digit
        mov       eax, r10d
        sub       eax, 2
        cmp       eax, 34
        ja        e_inval_wrote

        ;================ emit the digits FORWARD into the scratch ================
        lea       rdi, [rsp + 0A8h]            ; one past the scratch; every store pre-decrements
        mov       rax, rcx                     ; magnitude, treated as unsigned
        cmp       r10d, 10
        je        d10
        lea       r9d, [r10 - 1]               ; power-of-two radixes shift instead of dividing
        test      r9d, r10d
        jnz       dgen
        mov       r11d, r9d                    ; mask = radix-1
        bsf       ecx, r10d                    ; shift = log2(radix)
        jmp       dpow

dgen:
        mov       ecx, r10d
dgen_l:
        xor       rdx, rdx
        div       rcx
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        dgen_s
        lea       r9d, [rdx + 57h]
dgen_s:
        sub       rdi, 2
        mov       word ptr [rdi], r9w
        test      rax, rax
        jnz       dgen_l
        jmp       emitted

d10:
        lea       r8, dec2w
        mov       ecx, 100
d10_l:
        cmp       rax, 100
        jb        d10_last
        xor       rdx, rdx
        div       rcx
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
        shr       rax, cl
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
s_sw:
        mov       r9w, word ptr [rdi]
        mov       word ptr [rdx], r9w
        add       rdi, 2
        add       rdx, 2
        dec       rax
        jnz       s_sw
s_done:
        mov       word ptr [rdx], 0
        xor       eax, eax
        jmp       epi

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
wia_i64tow_s ENDP
END
