; ntdll.dll!RtlCharToInteger  --  hand-written x86-64 reimplementation (1.25x vs shipped)
; source of truth: changes/129-rtlchartointeger/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/129-rtlchartointeger/impl.asm
; NTSTATUS wia_char2int(PCSZ String, ULONG Base, PULONG Value)   [Win64: rcx, edx, r8 -> eax]
;
; Reimplements ntdll!RtlCharToInteger -- the parse-side complement of the landed 097 RtlIntegerToChar.
; Contract (reverse-engineered and validated bit-exact vs the live export):
;   1. skip while (signed char)*s <= ' '  -- a SIGNED compare, so it skips 0x01-0x20 AND 0x80-0xFF;
;   2. one optional '+' or '-' (whitespace is skipped only BEFORE the sign: "- 42" yields 0);
;   3. Base == 0 auto-detects "0x"/"0b"/"0o" -- LOWERCASE ONLY ("0X10" parses as decimal 0) -- and a
;      bare leading '0' means DECIMAL, not octal ("0777" -> 777);
;   4. Base outside {0,2,8,10,16} -> STATUS_INVALID_PARAMETER and *Value is left UNTOUCHED;
;   5. digits accumulate mod 2^32 with NO overflow detection ("4294967296" -> 0);
;   6. '-' negates mod 2^32; the status is STATUS_SUCCESS even when no digits are present.
;
; ISA: baseline x86-64, 256-entry digit-value table. Validated on Zen3.

.const
ALIGN 16
dgval:
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 000h,001h,002h,003h,004h,005h,006h,007h,008h,009h,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,00Ah,00Bh,00Ch,00Dh,00Eh,00Fh,010h,011h,012h,013h,014h,015h,016h,017h,018h
        db 019h,01Ah,01Bh,01Ch,01Dh,01Eh,01Fh,020h,021h,022h,023h,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,00Ah,00Bh,00Ch,00Dh,00Eh,00Fh,010h,011h,012h,013h,014h,015h,016h,017h,018h
        db 019h,01Ah,01Bh,01Ch,01Dh,01Eh,01Fh,020h,021h,022h,023h,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh

.code
wia_char2int PROC
        ; ---- A LEADING NUL IS STEPPED OVER. ONE. AT INDEX 0 ONLY.
        ;
        ; This is a real contract rule and this implementation shipped without it, returning 0 for every
        ; string whose first byte is a terminator while the export parses from byte 1. probes/pastnul.c
        ; and probes/pastnul2.c mapped it over 23 planted-byte cases and ONE rule explains every one of
        ; them with no contradictions:
        ;
        ;       00 36           -> 6            00 00 36        -> 0     (a second NUL stops it)
        ;       00 34 32        -> 42           20 00 38        -> 0     (a leading SPACE is not
        ;       00 20 36        -> 6                                      double-skipped: index 0 only)
        ;       00 80 36        -> 6            09 00 38        -> 0
        ;       00 2D 36        -> -6           2D 00 39        -> 0
        ;       00 2B 36        -> 6            20 00 00 38     -> 0
        ;       00 31 00 32     -> 1            00              -> 0
        ;       00 30 78 31 66  -> 1F (base 0, the prefix works from byte 1 too)
        ;       00 30 37 37 37  -> 777 (base 0, and still DECIMAL, not octal)
        ;
        ; So: if byte 0 is NUL, step over it, then run the ordinary skip/sign/parse from byte 1. It is
        ; almost certainly an off-by-one in the shipped code's end test -- the leading skip uses a SIGNED
        ; compare against ' ', and 00 satisfies it -- but the shape is crisp and total, and a drop-in has
        ; to reproduce it.
        ;
        ; WHY THE CORRECTNESS GATE PASSED FOR YEARS WITHOUT THIS. Its no-digit cases are string LITERALS
        ; ("" and "abc"), so what follows the terminator is whatever the linker put there -- and it
        ; happened to yield 0 for all sixteen bases, which is exactly what this implementation returned.
        ; The corpus could not express a controlled byte after the NUL, so it could not see the rule. It
        ; took the live-substitution gate, whose case buffer is a REUSED static array holding the
        ; previous case's bytes, to expose it: 291 differences out of 40000, every one a string starting
        ; with a terminator. A corpus of string literals cannot ask this question at all.
        cmp       byte ptr [rcx], 0
        jne       c_skip
        inc       rcx

        ; ---- skip leading: signed compare, so 0x80-0xFF are skipped too ----
c_skip:
        movsx     eax, byte ptr [rcx]
        test      al, al
        jz        c_sign
        cmp       eax, 20h
        jg        c_sign
        inc       rcx
        jmp       c_skip
c_sign:
        ; al still holds the character at [rcx] on entry here, and the base-0 path below needs exactly
        ; that byte. The first version reloaded it (`cmp byte ptr [rcx], '0'`), which costs a second L1
        ; access in the latency chain of the two rows that were still regressing -- "0777" and "0X10",
        ; both base 0 with a leading '0'. Reloading is only necessary when a sign was actually consumed
        ; and rcx moved, so the reload moved into that branch. The no-sign path (every row in the bench
        ; but two) reaches the comparison with the byte already in a register, and the instruction count
        ; is unchanged at five either way.
        xor       r9d, r9d                        ; neg = 0
        cmp       al, '+'
        je        c_signed
        cmp       al, '-'
        jne       c_base                          ; no sign at all: al is still the byte at [rcx]
        mov       r9d, 1
c_signed:
        inc       rcx
        movzx     eax, byte ptr [rcx]             ; a sign moved rcx, so this one must reload
c_base:
        ; ---- A BASE WE CHOSE OURSELVES DOES NOT NEED VALIDATING.
        ;
        ; Every exit from this block sets edx to 10, 2, 8 or 16, all of which are valid by construction,
        ; so each one jumps straight to c_go and skips the validity ladder entirely. Only a
        ; CALLER-SUPPLIED base can be wrong, and that is the one path that still walks the ladder.
        ; Worth two instructions on every base-0 call, which is what the last two rows below parity
        ; needed: "0777" and "0X10" were each about a cycle short.
        test      edx, edx                        ; Base == 0 -> auto-detect
        jnz       c_valid                         ; caller-supplied: validate it
        mov       edx, 10                         ; bare leading '0' means DECIMAL
        cmp       al, '0'
        jne       c_go
        movzx     eax, byte ptr [rcx + 1]
        sub       eax, 'b'
        cmp       eax, 22
        ja        c_go
        cmp       al, 22
        je        c_b16
        cmp       al, 13
        je        c_b8
        test      al, al
        jz        c_b2
        jmp       c_go
c_b16:  mov       edx, 16
        add       rcx, 2
        jmp       c_go
c_b2:   mov       edx, 2
        add       rcx, 2
        jmp       c_go
c_b8:   mov       edx, 8
        add       rcx, 2
        jmp       c_go                            ; must jump: c_valid no longer falls through to here
c_valid:
        ; ---- VALIDATING A CALLER-SUPPLIED BASE.
        ;
        ; Only a base the CALLER passed can be wrong -- the auto-detect block above jumps straight to
        ; c_go with a base it chose itself -- so this ladder now sits on one path instead of two, and it
        ; is shaped for what that path actually does.
        ;
        ; It used to be four compares in the order 10, 16, 8, 2, which charged an INVALID base all four
        ; before refusing. Measured over fifteen runs, the "invalid base" row (base 36) had a median of
        ; 0.94x and was the last real regression in the table; every other row was at or above parity.
        ;
        ; Decimal keeps its single compare, because it is the most common base and the short decimal rows
        ; have the least headroom. Everything else is then settled by range and a bitmask: any base above
        ; 16 is invalid, which refuses 36 in two more instructions instead of six, and the remaining
        ; candidates are checked against bits 2, 8 and 16 at once. Base 16 and base 2 pay one extra
        ; instruction each out of a 1.93x and 1.38x surplus.
        cmp       edx, 10
        je        c_go                            ; decimal: unchanged, two instructions
        cmp       edx, 16
        ja        c_bad                           ; above 16 cannot be valid: *Value untouched
        mov       r10d, 10104h                    ; bits 2, 8 and 16
        bt        r10d, edx
        jnc       c_bad
c_go:
        xor       eax, eax                        ; value (mod 2^32, no overflow check)
        ; (per-base specialised loops for 10/16 were tried and measured SLOWER -- the dispatch
        ;  branches cost more than the shortened multiply chain saves. Kept the single loop.)
ALIGN 16
c_loop:
        ; digit value without a table: the 0-9 fast path is 3 instructions and, unlike a
        ; 256-entry lookup, costs no second dependent load in the latency chain.
        movzx     r10d, byte ptr [rcx]
        lea       r11d, [r10 - '0']
        cmp       r11d, 9
        jbe       c_have

        ; ---- FOR BASE <= 10 A LETTER CAN NEVER BE A DIGIT, SO DO NOT DECODE ONE.
        ;
        ; Below this point the byte is not '0'-'9', and the only remaining way it could be a digit is as
        ; a letter -- which requires a digit value of at least 10, so it is impossible for base 2, 8 and
        ; 10. Those three bases can stop right here, and the case fold, the range check and the compare
        ; against the base underneath are all dead work for them.
        ;
        ; EVERY decimal parse reaches this point exactly once, on its terminating NUL, so this shortcut
        ; pays on every base-10 and base-8 row rather than only on the awkward ones. It was measured
        ; because two rows would not come up to parity -- "0777" and "0X10", base 0 with a leading '0' --
        ; and in both of those the character that ends the parse is decoded as a letter, given a digit
        ; value of 33 for 'X' or rejected outright for the NUL, and then thrown away.
        ;
        ; Base 16 pays two extra instructions on its final byte only, out of a 1.76x-1.86x surplus.
        cmp       edx, 10
        jbe       c_done

        or        r10d, 20h                       ; fold case
        sub       r10d, 'a'
        cmp       r10d, 25
        ja        c_done
        lea       r11d, [r10 + 10]
c_have:
        cmp       r11d, edx
        jae       c_done
        imul      eax, edx
        add       eax, r11d
        inc       rcx
        jmp       c_loop
c_done:
        test      r9d, r9d
        jz        c_store
        neg       eax
c_store:
        mov       [r8], eax
        xor       eax, eax                        ; STATUS_SUCCESS
        ret
c_bad:
        mov       eax, 0C000000Dh                 ; STATUS_INVALID_PARAMETER
        ret
wia_char2int ENDP
END
