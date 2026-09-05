; ntdll.dll!RtlLargeIntegerToChar  --  hand-written x86-64 reimplementation (1.43x vs shipped)
; source of truth: changes/100-rtllargeintegertochar/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/100-rtllargeintegertochar/impl.asm
; NTSTATUS wia_litoc(PLARGE_INTEGER Value, ULONG Base, LONG Length, PCHAR String)
;   [rcx=&Value, edx=Base, r8d=Length, r9=String -> eax]
;
; Reimplements ntdll!RtlLargeIntegerToChar: the 64-bit sibling of RtlIntegerToChar (097).
; Same contract: format the unsigned 64-bit *Value in Base (0 -> 10; only 2/8/10/16 valid,
; else STATUS_INVALID_PARAMETER 0xC000000D) as uppercase ASCII digits. `Length` is the output
; buffer capacity: writes the minimal digit string left-justified at String[0..dc-1] and a NUL
; at String[dc] iff Length > dc; if dc > Length returns STATUS_BUFFER_OVERFLOW (0x80000005)
; and writes nothing. (Verified vs the live export.) Base 10 emits two digits/iteration from a
; 200-byte table; 2/16 write straight to the buffer MSB-first sized by lzcnt (no temp/copy);
; octal uses a stack temp. ISA: baseline x64 + LZCNT. Validated on Zen3.

.const
ALIGN 16
dec2b   db "00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899"
hexlut16 db "0123456789ABCDEF"

.code
wia_litoc PROC
        push      rbx
        push      rsi
        push      rdi
        sub       rsp, 96                            ; temp[96] (octal of 64-bit -> 22 digits)
        mov       rax, qword ptr [rcx]               ; 64-bit value
        mov       r10d, r8d                          ; Length (capacity)
        mov       r11, r9                            ; String
        mov       r8d, edx                           ; base
        test      r8d, r8d
        jnz       have_base
        mov       r8d, 10
have_base:
        lea       rsi, [rsp + 96]                    ; end of temp; build downward
        cmp       r8d, 10
        je        do10
        cmp       r8d, 16
        je        do16
        cmp       r8d, 8
        je        do8
        cmp       r8d, 2
        je        do2
        mov       eax, 0C000000Dh                    ; STATUS_INVALID_PARAMETER
        jmp       epi
do10:
        lea       rdi, [dec2b]
d10_loop:
        cmp       rax, 100
        jb        d10_last
        xor       edx, edx
        mov       ecx, 100
        div       rcx                                ; rax=q, rdx=r (0..99)
        movzx     ecx, word ptr [rdi + rdx*2]
        sub       rsi, 2
        mov       word ptr [rsi], cx
        jmp       d10_loop
d10_last:
        cmp       eax, 10
        jb        d10_one
        movzx     ecx, word ptr [rdi + rax*2]
        sub       rsi, 2
        mov       word ptr [rsi], cx
        jmp       emit_done
d10_one:
        add       eax, 30h
        dec       rsi
        mov       byte ptr [rsi], al
        jmp       emit_done
        ; ---- hex: dc = ceil(sigbits/4); write MSB-first straight to the buffer ----
do16:
        test      rax, rax
        jz        emit_zero
        lzcnt     rcx, rax
        mov       edx, 64
        sub       edx, ecx
        add       edx, 3
        shr       edx, 2                             ; dc = ceil(sigbits/4)  (<= 16)
        cmp       edx, r10d
        ja        overflow
        lea       rdi, [hexlut16]
        mov       r9, r11
        lea       ecx, [edx-1]
        shl       ecx, 2                             ; start shift = (dc-1)*4
h16w:
        mov       r8, rax
        shr       r8, cl                             ; 64-bit shift
        and       r8d, 0Fh
        movzx     r8d, byte ptr [rdi + r8]
        mov       byte ptr [r9], r8b
        inc       r9
        sub       ecx, 4
        jns       h16w
        cmp       r10d, edx
        jbe       ret_ok
        mov       byte ptr [r9], 0
        jmp       ret_ok
        ; ---- octal: rare -> temp + copy ----
do8:
        mov       rcx, rax
        and       ecx, 7
        add       ecx, 30h
        dec       rsi
        mov       byte ptr [rsi], cl
        shr       rax, 3
        jnz       do8
        jmp       emit_done
        ; ---- binary: dc = sigbits; write MSB-first straight to the buffer ----
do2:
        test      rax, rax
        jz        emit_zero
        lzcnt     rcx, rax
        mov       edx, 64
        sub       edx, ecx                           ; dc = sigbits (<= 64)
        cmp       edx, r10d
        ja        overflow
        mov       r9, r11
        lea       ecx, [edx-1]                       ; start shift = dc-1
b2w:
        mov       r8, rax
        shr       r8, cl
        and       r8d, 1
        add       r8d, 30h
        mov       byte ptr [r9], r8b
        inc       r9
        sub       ecx, 1
        jns       b2w
        cmp       r10d, edx
        jbe       ret_ok
        mov       byte ptr [r9], 0
        jmp       ret_ok
        ; ---- value 0 (any base): "0" + NUL if room ----
emit_zero:
        test      r10d, r10d
        jz        overflow
        mov       byte ptr [r11], '0'
        cmp       r10d, 1
        jbe       ret_ok
        mov       byte ptr [r11 + 1], 0
ret_ok:
        xor       eax, eax
        jmp       epi
        ; ---- decimal / octal: dc from temp, bounds-check, copy ----
emit_done:
        lea       rax, [rsp + 96]
        sub       rax, rsi                           ; dc = digit count
        cmp       eax, r10d
        ja        overflow
        mov       rdi, r11
        mov       ebx, eax
        mov       ecx, eax
copy_lp:
        test      ecx, ecx
        jz        copy_done
        mov       dl, byte ptr [rsi]
        mov       byte ptr [rdi], dl
        inc       rsi
        inc       rdi
        dec       ecx
        jmp       copy_lp
copy_done:
        cmp       r10d, ebx
        jbe       no_nul
        mov       byte ptr [rdi], 0
no_nul:
        xor       eax, eax
        jmp       epi
overflow:
        mov       eax, 80000005h                     ; STATUS_BUFFER_OVERFLOW
epi:
        add       rsp, 96
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_litoc ENDP
END
