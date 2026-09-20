; changes/097-rtlintegertochar/impl_2ndpc.asm
;==============================================================================
; 2ND PC VARIANT,  AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445
;==============================================================================
; The original `impl.asm` in this directory is UNTOUCHED and remains the 5950X
; (Zen 3) implementation of record. This file is an ADDITIONAL variant tuned for
; the second PC. Same exported symbol (`wia_itoc`), so the existing correctness.c
; and bench.c validate it unmodified, build it with build_2ndpc.bat.
;
; Why a 2ND-PC variant is needed
; ------------------------------
; Re-measured on this machine, the Zen 3 implementation REGRESSED on one size
; class and therefore failed the project's own gate (a regression on ANY class
; parks the change):
;
;     size        ours ns    system ns   ratio   verdict
;     dec-1d         4.45         3.10   0.70x   WORSE     <-- gate failure
;     dec-3d         ...                 1.3x+   BETTER
;     dec-5d/10d     ...                 1.5-2x  BETTER
;     hex-8d/bin-32d ...                 1.1x+   BETTER
;     geomean 1.327x => PARKED (a size class regressed)
;
; Cause (this is a dispatch-floor loss, not a Zen-4 correctness issue): to format
; a SINGLE decimal digit the Zen 3 path still pays the full general prologue --
; `push rbx/rsi/rdi`, a 64-byte stack frame, a build-into-temp-from-the-end pass,
; and then a byte-at-a-time `copy_lp` loop, in order to emit two bytes ("5", 0).
; That fixed overhead is ~7 ns of work to produce 2 bytes. ntdll's scalar routine
; has almost no prologue, so at one digit it simply wins. Zen 4 did not create
; this weakness; it exposed it, because this PC's newer ntdll (26100.9278) got
; cheaper on the short path while our fixed cost stayed constant.
;
; THE FIX
; -------
; A frameless fast path for the overwhelmingly common case, base 10 (or base 0,
; which means 10) with Value < 100, placed BEFORE the prologue. It touches no
; callee-saved register, allocates no stack, uses no temp buffer and no copy loop:
; one or two stores and a return. Everything else falls through to the original
; body, byte-for-byte unchanged.
;
; Contract preserved exactly (all re-verified against the live export):
;   * Base 0 is treated as base 10.
;   * dc (digit count) is 1 for Value 0..9, 2 for Value 10..99.
;   * dc > Length  -> STATUS_BUFFER_OVERFLOW (0x80000005) and nothing is written.
;   * digits are written left-justified at String[0..dc-1].
;   * a NUL is written at String[dc] IFF Length > dc.
;   * success returns STATUS_SUCCESS (0).
;   * Length is compared UNSIGNED, exactly as the original does (`cmp eax,r10d` /
;     `ja overflow`). A negative Length therefore behaves as a huge capacity on
;     both paths; a quirk of the shipped routine that is deliberately mirrored
;     rather than "fixed", since bit-exactness is the gate.
;
; SAFETY
;   * No vector registers, no AVX-512, no GFNI; this variant is plain scalar
;     x86-64 and runs correctly on the 5950X too (it is simply unnecessary there).
;   * Writes at most 3 bytes, all within the caller's buffer and only after the
;     same capacity check the original performs. No read of any input buffer.
;
; NTSTATUS wia_itoc(ULONG Value, ulong Base, long Length, pchar String)
;   [rcx=Value, edx=Base, r8d=Length, r9=String -> eax]

.const
ALIGN 16
dec2b   db "00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899"
hexlut16 db "0123456789ABCDEF"

.code
wia_itoc PROC
        ;----------------------------------------------------------------------
        ; 2ND PC FAST PATH: base 0/10 and Value < 100. Frameless: no push, no
        ; stack frame, no temp build, no copy loop. This is the entire fix.
        ;----------------------------------------------------------------------
        cmp       ecx, 100                           ; Value < 100 ?
        jae       slow_path
        test      edx, edx                           ; Base == 0 (means 10) ?
        jz        fast_dec
        cmp       edx, 10                            ; Base == 10 ?
        jne       slow_path
fast_dec:
        cmp       ecx, 10
        jae       fast_two                           ; 10..99 -> two digits

        ; ---- one decimal digit (dc = 1) ----
        cmp       r8d, 1                             ; UNSIGNED, mirrors original
        jb        fast_overflow                      ; dc > Length -> overflow
        mov       eax, ecx
        add       eax, 30h                           ; '0' + Value
        mov       byte ptr [r9], al
        cmp       r8d, 1
        jbe       fast_ok                            ; Length <= dc -> no NUL
        mov       byte ptr [r9 + 1], 0               ; NUL at String[dc]
fast_ok:
        xor       eax, eax                           ; STATUS_SUCCESS
        ret

        ; ---- two decimal digits (dc = 2) ----
fast_two:
        cmp       r8d, 2                             ; UNSIGNED, mirrors original
        jb        fast_overflow
        lea       rax, [dec2b]
        movzx     eax, word ptr [rax + rcx*2]        ; both ASCII digits at once
        mov       word ptr [r9], ax
        cmp       r8d, 2
        jbe       fast_ok2                           ; Length <= dc -> no NUL
        mov       byte ptr [r9 + 2], 0
fast_ok2:
        xor       eax, eax
        ret

fast_overflow:
        mov       eax, 80000005h                     ; STATUS_BUFFER_OVERFLOW
        ret

        ;----------------------------------------------------------------------
        ; ORIGINAL BODY, byte-for-byte identical to impl.asm from here down.
        ;----------------------------------------------------------------------
slow_path:
        push      rbx
        push      rsi
        push      rdi
        sub       rsp, 64                            ; temp[64] (>= 32 binary digits)
        mov       eax, ecx                           ; value
        mov       r10d, r8d                          ; Length (capacity)
        mov       r11, r9                            ; String
        mov       r8d, edx                           ; base
        test      r8d, r8d
        jnz       have_base
        mov       r8d, 10
have_base:
        lea       rsi, [rsp + 64]                    ; end of temp; build downward
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
        cmp       eax, 100
        jb        d10_last
        xor       edx, edx
        mov       ecx, 100
        div       ecx                                ; eax=q, edx=r (0..99)
        movzx     ecx, word ptr [rdi + rdx*2]        ; two ASCII digits
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
        ; ---- hex: dc = ceil(sigbits/4); write MSB-first straight to the buffer (no temp/copy)
do16:
        test      eax, eax
        jz        emit_zero
        lzcnt     ecx, eax
        mov       edx, 32
        sub       edx, ecx
        add       edx, 3
        shr       edx, 2                             ; dc = ceil(sigbits/4)
        cmp       edx, r10d
        ja        overflow                           ; dc > Length
        lea       rdi, [hexlut16]
        mov       r9, r11                            ; out
        lea       ecx, [edx-1]
        shl       ecx, 2                             ; start shift = (dc-1)*4
h16w:
        mov       r8d, eax
        shr       r8d, cl
        and       r8d, 0Fh
        movzx     r8d, byte ptr [rdi + r8]
        mov       byte ptr [r9], r8b
        inc       r9
        sub       ecx, 4
        jns       h16w
        cmp       r10d, edx
        jbe       ret_ok
        mov       byte ptr [r9], 0                   ; NUL (Length > dc)
        jmp       ret_ok
        ; ---- octal: rare -> temp + copy (still correct, goes through emit_done)
do8:
        mov       ecx, eax
        and       ecx, 7
        add       ecx, 30h
        dec       rsi
        mov       byte ptr [rsi], cl
        shr       eax, 3
        jnz       do8
        jmp       emit_done
        ; ---- binary: dc = sigbits; write MSB-first straight to the buffer
do2:
        test      eax, eax
        jz        emit_zero
        lzcnt     ecx, eax
        mov       edx, 32
        sub       edx, ecx                           ; dc = 32 - lzcnt
        cmp       edx, r10d
        ja        overflow
        mov       r9, r11
        lea       ecx, [edx-1]                       ; start shift = dc-1
b2w:
        mov       r8d, eax
        shr       r8d, cl
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
        jz        overflow                           ; Length 0 -> overflow
        mov       byte ptr [r11], '0'
        cmp       r10d, 1
        jbe       ret_ok
        mov       byte ptr [r11 + 1], 0
ret_ok:
        xor       eax, eax
        jmp       epi
emit_done:
        lea       rax, [rsp + 64]
        sub       rax, rsi                           ; dc = digit count
        cmp       eax, r10d
        ja        overflow                           ; dc > Length -> overflow
        mov       rdi, r11                           ; String
        mov       ebx, eax                           ; dc (kept for NUL decision)
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
        jbe       no_nul                             ; Length <= dc -> no NUL
        mov       byte ptr [rdi], 0                  ; NUL at String[dc]
no_nul:
        xor       eax, eax                           ; STATUS_SUCCESS
        jmp       epi
overflow:
        mov       eax, 80000005h                     ; STATUS_BUFFER_OVERFLOW
epi:
        add       rsp, 64
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_itoc ENDP
END
