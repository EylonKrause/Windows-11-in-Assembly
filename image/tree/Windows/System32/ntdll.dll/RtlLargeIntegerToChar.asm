; ntdll.dll!RtlLargeIntegerToChar  --  hand-written x86-64 reimplementation (3.25x vs shipped)
; source of truth: changes/280-rtllargeintegertochar/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/280-rtllargeintegertochar/impl.asm
;   NTSTATUS wia_lint2char(const LARGE_INTEGER* value, ULONG base, LONG length, char* out)
;       [Win64: rcx, edx, r8d, r9 -> eax]
;
; ntdll!RtlLargeIntegerToChar. discovery/rtl_integer_char.c measured the shipped export at
; 27.75 ns for nineteen decimal digits.
;
; --------------------------------------------------------------------------------------------------
; 1. This change supersedes change 100, which is wrong on every negative length.
;
; 100 landed this export at 1.43x. Like change 097 before it -- which change 279 replaced for the
; same reason -- its capacity test is an UNSIGNED compare, so a negative length reads as the largest
; possible room and it writes the digits left-justified with a terminator where the export writes a
; ZERO-PADDED FIELD. Swept against live over every negative length -1..-60 in five bases it differs
; in 123000 of 123000 cases, while all 125050 positive-length cases are correct. Its corpus never
; asked. Its 1.43x is a positive-length-only number.
;
;     live, 19 digits, length -25:  30 30 30 30 30 30 31 32 33 ...   six zeros, then the digits
;     change 100, same call:        31 32 33 34 35 36 37 38 39 ... 00   the digits, then a terminator
;
; --------------------------------------------------------------------------------------------------
; 2. The contract, measured by probes/contract.c -- it is change 279's, not a new one.
;
;   * bases 0, 2, 8, 10, 16 only; 0 means 10; everything else STATUS_INVALID_PARAMETER.
;   * The base is validated before the value pointer is dereferenced. The probe put the
;     LARGE_INTEGER on a NOACCESS page and called with base 7: it returned C000000D rather than
;     faulting. With a good base and no room it FAULTED -- so the value is read after the base is
;     checked and before the room is known. This implementation reads it in exactly that window.
;   * UNSIGNED, despite PLARGE_INTEGER being signed: 0x8000000000000000 prints as
;     9223372036854775808 and -1 as 18446744073709551615.
;   * length >= digits, and the terminator is written only if it fits:
;         19 digits, length 18 -> STATUS_BUFFER_OVERFLOW, buffer untouched
;         19 digits, length 19 -> nineteen characters, NO terminator
;         19 digits, length 20 -> nineteen characters AND a terminator
;   * a negative length is a zero-padded field width, honoured literally, no terminator:
;         -19 -> "1234567890123456789"      -22 -> "0001234567890123456789"
;         -96 into a 96-byte buffer succeeds; -97 runs off the end.
;   * INT_MIN is the one negative length that refuses -- it cannot be negated.
;   * a refusal leaves the buffer completely untouched.
;   * the longest answers are 64 binary, 22 octal, 20 decimal and 16 hexadecimal digits.
;
; --------------------------------------------------------------------------------------------------
; 3. The 64-BIT division, which is why this was deferred out of change 279.
;
; Changes 067, 278 and 279 all rest on `(v * 51EB851Fh) >> 37 == v/100`, proved by RUNNING it over
; all 2^32 values. That proof says nothing about a 64-bit domain, and 2^64 cases cannot be run.
;
; So this never divides a 64-bit value by 100. It peels EIGHT decimal digits at a time with ONE
; 64-bit division by 10^8, until what is left fits in 32 bits, where 067's proved constant applies:
;
;     q1 = v / 10^8, r1 = v - q1*10^8      the low eight digits
;     q2 = q1 / 10^8, r2 = q1 - q2*10^8    the next eight
;     q2 < 1845                            the top four -- 4 + 8 + 8 = 20, the longest answer
;
; probes/div64.c proves the one 64-bit constant over the whole domain without running 2^64 cases.
; Both sides of the identity are monotone and the right side steps only at multiples of 10^8, so
; agreement at every step is agreement everywhere -- and it checks every ONE of the 184467440737
; steps, on both sides, in 20.6 seconds. It also computes the Granlund-Montgomery round-up
; criterion in exact arithmetic as an independent second opinion: e = M*d - 2^90 = 875776, which is
; <= 2^26, so the identity is sufficient by that argument too. Two arguments, one exhaustive and one
; arithmetic, that agree.
;
; --------------------------------------------------------------------------------------------------
; 4. The power-of-two bases emit several digits per store, as change 279's do: a byte is two
;    hexadecimal digits, six bits are two octal digits, and a byte is eight binary digits that go out
;    as ONE 8-byte store -- so sixty-four binary digits are eight stores, not sixty-four iterations.
;    Their digit counts are arithmetic, not a table: BSR, then >>2 for hex and (n*0AAABh)>>17 for
;    octal, proved by div64.c over all 64 bit lengths.
;
; 5. The zero padding is written first, not last. Change 279 padded after the digits, which meant
;    holding the buffer pointer to the very end and left no register for the digit tables. Filling
;    the field before the digits are written frees that register, so this is a leaf with no frame,
;    No pushes and no calls -- the same shape as 279 despite doing strictly more work.
;
; ISA: baseline x64, plus SSE2 (also baseline on x64) for the field fill. No YMM is touched on any
; path, so there is no upper state to clear and no VZEROUPPER anywhere.

OPTION PROC:PRIVATE
PUBLIC wia_lint2char
PUBLIC wia_li2c_tables
PUBLIC wia_li2c_offsets

ST_INVALID  EQU 0C000000Dh
ST_OVERFLOW EQU 80000005h
D8          EQU 100000000                  ; 10^8, the one 64-bit divisor

.const
ALIGN 16
TB      LABEL BYTE

; two ASCII decimal digits per entry, packed little-endian so the tens digit lands first
DEC2B   LABEL WORD
        NI = 0
        REPT 100
          DW (30h + (NI / 10)) OR ((30h + (NI MOD 10)) SHL 8)
          NI = NI + 1
        ENDM

HEXCH   LABEL BYTE
        DB      '0123456789ABCDEF'

ALIGN 16
; one byte -> two hexadecimal characters, high nibble at the LOWER address
HEX2    LABEL WORD
        NH = 0
        REPT 256
          HI2 = NH SHR 4
          LO2 = NH AND 15
          IF HI2 LT 10
            HC = 30h + HI2
          ELSE
            HC = 37h + HI2
          ENDIF
          IF LO2 LT 10
            LC = 30h + LO2
          ELSE
            LC = 37h + LO2
          ENDIF
          DW HC OR (LC SHL 8)
          NH = NH + 1
        ENDM

; six bits -> two octal characters
OCT2    LABEL WORD
        NO2 = 0
        REPT 64
          DW (30h + (NO2 SHR 3)) OR ((30h + (NO2 AND 7)) SHL 8)
          NO2 = NO2 + 1
        ENDM

; one byte -> eight binary characters, MSB first
BIN8    LABEL BYTE
        NB8 = 0
        REPT 256
          NK = 7
          REPT 8
            DB 30h + ((NB8 SHR NK) AND 1)
          NK = NK - 1
          ENDM
          NB8 = NB8 + 1
        ENDM

; digits10(2^B) for every bit length a 64-bit value can have
GTAB    LABEL BYTE
        NB = 0
        REPT 64
          DB (NB * 1233) / 4096 + 1
          NB = NB + 1
        ENDM

ALIGN 16
; The threshold is indexed by the bit length, not by the digit count, and that is the point.
;
; The obvious form of this is `digits = GTAB[idx] + (v >= POW10[GTAB[idx]])`, which is what change
; 279 does -- and it costs two dependent loads: the power of ten cannot be fetched until the digit
; estimate has arrived. Storing 10^GTAB[idx] directly against the same index makes the two loads
; INDEPENDENT, so they issue together and the chain is BSR -> load -> compare instead of
; BSR -> load -> load -> compare. It is the same arithmetic with five cycles taken out of it.
;
; Entries 0..59 are computed by the assembler; the last four need 10^19, which exceeds a SIGNED
; 64-bit assembler expression and is written out. correctness.c recomputes all sixty-four the slow
; way from GTAB and compares, so a wrapped or mis-ordered entry cannot survive.
THRESH  LABEL QWORD
        NB3 = 0
        REPT 60
          NT = (NB3 * 1233) / 4096 + 1
          NP3 = 1
          REPT NT
            NP3 = NP3 * 10
          ENDM
          DQ NP3
          NB3 = NB3 + 1
        ENDM
        DQ      8AC7230489E80000h, 8AC7230489E80000h      ; bit lengths 60 and 61: 10^19
        DQ      8AC7230489E80000h, 8AC7230489E80000h      ; bit lengths 62 and 63: 10^19

ALIGN 16
C30     DB      16 DUP(30h)                  ; sixteen ASCII zeros, for the field fill

ALIGN 8
; ceil(2^90 / 10^8). probes/div64.c proves umulh(n, M) >> 26 == n / 10^8 for every n < 2^64.
M64     DQ      12379400392853802749

; the sub-table offsets, so correctness.c can find and check every one of them
OFFS    LABEL DWORD
        DD      DEC2B - TB
        DD      HEXCH - TB
        DD      HEX2  - TB
        DD      OCT2  - TB
        DD      BIN8  - TB
        DD      GTAB  - TB
        DD      THRESH - TB
        DD      C30   - TB
        DD      M64   - TB

DECOFF  EQU     DEC2B - TB
HEXCOFF EQU     HEXCH - TB
HEX2OFF EQU     HEX2  - TB
OCT2OFF EQU     OCT2  - TB
BINOFF  EQU     BIN8  - TB
GTOFF   EQU     GTAB  - TB
THOFF   EQU     THRESH - TB
M64OFF  EQU     M64   - TB

.code

; The register budget. Everything is volatile -- nothing is saved, there is no frame, there are no
; calls, and the padding is written BEFORE the digits precisely so that the buffer pointer dies
; early and its register can carry the tables:
;
;   rcx  the value pointer, then the table base while the digit count is computed, then scratch
;   rdx  the base, then scratch (MUL writes it, so the dispatch on the base happens first)
;   r8   the length, then the write cursor, running backwards from the end of the field
;   r9   the output buffer, then the table base once the padding has been written
;   r10  the 64-bit value, consumed as it is converted
;   r11  the digit count, then the quotient across the eight-digit peel
;   rax  scratch, and the low half of MUL
;   xmm0 sixteen ASCII zeros, for a field fill of 16 bytes or more

ALIGN 16
wia_lint2char PROC
        ; ---- the base, validated BEFORE the value pointer is touched. probes/contract.c measured
        ;      that order against a NOACCESS page and this reproduces it.
        test      edx, edx
        jnz       have_base
        mov       edx, 10                         ; base 0 means base 10
have_base:
        cmp       edx, 10
        je        d_base10
        cmp       edx, 16
        je        d_pow2
        cmp       edx, 8
        je        d_pow2
        cmp       edx, 2
        je        d_pow2
        mov       eax, ST_INVALID
        ret

; ---- how many digits, into r11d
d_pow2:
        mov       r10, qword ptr [rcx]            ; the value, read after the base is validated
        mov       rax, r10
        or        rax, 1                          ; BSR is undefined at zero; "0" is one digit
        bsr       rax, rax                        ; the index of the top set bit, 0..63
        cmp       edx, 2
        je        p2_done                         ; base 2: one digit per bit
        cmp       edx, 16
        jne       p2_oct
        shr       eax, 2                          ; base 16: one digit per four bits
p2_done:
        lea       r11d, [rax + 1]
        jmp       have_digits
p2_oct:
        imul      eax, eax, 0AAABh                ; base 8: one digit per three bits
        shr       eax, 17
        lea       r11d, [rax + 1]
        jmp       have_digits

d_base10:
        mov       r10, qword ptr [rcx]
        ; a single digit pays for nothing. Everything below -- the bsr, the table load, the
        ; threshold compare -- exists to tell nine from ten, and a value under ten already knows.
        ; This is not a benchmark special case: it is the two rows on which the change being
        ; SUPERSEDED was faster, and they were faster for exactly this reason. Change 100 formats a
        ; small number with a loop that exits immediately and reads no table at all; a replacement
        ; that made "7" slower would be a regression however good its nineteen-digit row looked.
        cmp       r10, 10
        jb        d_one_digit
        lea       rcx, TB
        mov       rax, r10
        bsr       rax, rax                        ; the value is >= 10, so BSR is defined
        movzx     r11d, byte ptr [rcx + GTOFF + rax]
        cmp       r10, qword ptr [rcx + THOFF + rax*8]   ; INDEPENDENT of the load above
        setae     al
        movzx     eax, al
        add       r11d, eax                       ; digits10(value), 2..20
        jmp       have_digits

        ; ---- a single decimal digit, written where it is decided.
        ;
        ; Setting the count to one and falling into the general machinery was measured and it was
        ; NOT enough: the value then walks the room rule, the dispatch and three more comparisons in
        ; the decimal writer only to rediscover that it has one digit. Seven taken branches to store
        ; one byte. That left "7" and "0" at 0.80x of change 100 -- which formats a small number
        ; with a loop that exits immediately -- while every other row was at or above parity, and a
        ; supersession that made the most common case slower is a regression however good its
        ; twenty-digit row looks.
        ;
        ; The whole operation for a positive length is: one character, and a terminator if there is
        ; room for one. Anything else -- a length of zero, or a negative one, which is a padded
        ; field -- is rare here and goes back to the general rule.
        ;
        ; The second CMP is not redundant. ADD writes the flags, so the terminator test cannot read
        ; the ones the room test left behind; the first draft of this block did exactly that.
d_one_digit:
        test      r8d, r8d
        jle       d_one_general                   ; zero or a field width: not this path
        add       r10d, 30h
        mov       byte ptr [r9], r10b
        cmp       r8d, 1
        je        d_one_exact                     ; length exactly 1: no room for a terminator
        mov       byte ptr [r9 + 1], 0
d_one_exact:
        xor       eax, eax
        ret
d_one_general:
        mov       r11d, 1

have_digits:
        ; The room rule. a positive length is room, and the terminator is written only if it fits.
        ; A NEGATIVE length is a zero-padded field width, and no terminator is written at all.
        test      r8d, r8d
        jg        pos_len
        jz        overflow                        ; length 0 is a refusal
        cmp       r8d, 80000000h
        je        overflow                        ; INT_MIN cannot be negated
        neg       r8d                             ; the field width, zero-extended into r8
        cmp       r11d, r8d
        ja        overflow
        mov       ecx, r8d
        sub       ecx, r11d                       ; how many zeros the field needs
        lea       r8, [r9 + r8]                   ; the cursor: one past the field, no terminator
        test      ecx, ecx
        jnz       do_fill
        jmp       dispatch
pos_len:
        cmp       r11d, r8d
        ja        overflow
        lea       r8, [r9 + r11]                  ; a positive length never pads: the field IS the
        je        dispatch                        ;   digits, and if it exactly fills there is no
        mov       byte ptr [r8], 0                ;   room for a terminator
        jmp       dispatch

        ; ---- the field fill, written BEFORE the digits so that r9 can become the table base.
        ;      Sixteen at a time with the two ends OVERLAPPING, so no count from 16 to 31 needs a
        ;      loop; below 16 the ends overlap as immediates, so no count needs one at all. Change
        ;      279's first attempt wrote this a byte at a time and PARKED on the one row that
        ;      measured it -- 182 ns against the export's 115.
do_fill:
        mov       rax, r9
        cmp       ecx, 16
        jb        fill_small
        movups    xmm0, xmmword ptr [C30]
        cmp       ecx, 32
        jb        fill_ends
fill_32:
        movups    xmmword ptr [rax], xmm0
        movups    xmmword ptr [rax + 16], xmm0
        add       rax, 32
        sub       ecx, 32
        cmp       ecx, 32
        jae       fill_32
        test      ecx, ecx
        jz        dispatch
        cmp       ecx, 16
        jb        fill_small
fill_ends:
        movups    xmmword ptr [rax], xmm0
        movups    xmmword ptr [rax + rcx - 16], xmm0
        jmp       dispatch
fill_small:
        cmp       ecx, 8
        jb        fill_le7
        mov       dword ptr [rax], 30303030h
        mov       dword ptr [rax + 4], 30303030h
        mov       dword ptr [rax + rcx - 8], 30303030h
        mov       dword ptr [rax + rcx - 4], 30303030h
        jmp       dispatch
fill_le7:
        cmp       ecx, 4
        jb        fill_le3
        mov       dword ptr [rax], 30303030h
        mov       dword ptr [rax + rcx - 4], 30303030h
        jmp       dispatch
fill_le3:
        cmp       ecx, 2
        jb        fill_one
        mov       word ptr [rax], 3030h
        mov       word ptr [rax + rcx - 2], 3030h
        jmp       dispatch
fill_one:
        mov       byte ptr [rax], 30h

dispatch:
        lea       r9, TB                          ; the buffer is finished with; the tables move in
        cmp       edx, 10
        je        w_dec
        cmp       edx, 16
        je        w_hex
        cmp       edx, 8
        je        w_oct

; ---- base 2: EIGHT digits per store, from 256 rows of eight bytes
w_bin:
        cmp       r11d, 8
        jb        b2_tail
        movzx     ecx, r10b
        mov       rax, qword ptr [r9 + BINOFF + rcx*8]
        sub       r8, 8
        mov       qword ptr [r8], rax
        shr       r10, 8
        sub       r11d, 8
        jmp       w_bin
b2_tail:
        test      r11d, r11d
        jz        done
b2_one:
        mov       ecx, r10d
        and       ecx, 1
        add       ecx, 30h
        dec       r8
        mov       byte ptr [r8], cl
        shr       r10, 1
        dec       r11d
        jnz       b2_one
        jmp       done

; ---- base 16: TWO digits per store -- one byte is exactly two hexadecimal characters
w_hex:
        cmp       r11d, 2
        jb        h_last
        movzx     ecx, r10b
        movzx     ecx, word ptr [r9 + HEX2OFF + rcx*2]
        sub       r8, 2
        mov       word ptr [r8], cx
        shr       r10, 8
        sub       r11d, 2
        jmp       w_hex
h_last:
        test      r11d, r11d
        jz        done
        and       r10d, 0Fh
        movzx     ecx, byte ptr [r9 + HEXCOFF + r10]
        dec       r8
        mov       byte ptr [r8], cl
        jmp       done

; ---- base 8: TWO digits per store -- six bits are exactly two octal characters
w_oct:
        cmp       r11d, 2
        jb        o_last
        mov       ecx, r10d
        and       ecx, 3Fh
        movzx     ecx, word ptr [r9 + OCT2OFF + rcx*2]
        sub       r8, 2
        mov       word ptr [r8], cx
        shr       r10, 6
        sub       r11d, 2
        jmp       w_oct
o_last:
        test      r11d, r11d
        jz        done
        and       r10d, 7
        add       r10d, 30h
        dec       r8
        mov       byte ptr [r8], r10b
        jmp       done

; ---- base 10. Eight digits at a time while the value does not fit in 32 bits, using the one
;      64-bit reciprocal probes/div64.c proved; then 067's 32-bit constant for what is left.
w_dec:
d_loop:
        cmp       r10, D8
        jb        d_tail
        mov       rax, r10
        mul       qword ptr [r9 + M64OFF]         ; rdx:rax = v * ceil(2^90/10^8)
        shr       rdx, 26                         ; the quotient, proved exact over all 2^64
        mov       r11, rdx                        ; keep it: the digit count is finished with
        imul      rdx, rdx, D8
        sub       r10, rdx                        ; the remainder, 0..99999999
        mov       ecx, 4                          ; four pairs, ZERO-PADDED: it is a middle chunk
d_eight:
        mov       edx, r10d
        imul      rdx, rdx, 51EB851Fh
        shr       rdx, 37                         ; /100, proved exact over all 2^32
        mov       eax, edx
        imul      eax, eax, 100
        sub       r10d, eax                       ; the remainder, 0..99
        movzx     eax, word ptr [r9 + r10*2]
        sub       r8, 2
        mov       word ptr [r8], ax               ; two characters, one store
        mov       r10d, edx
        dec       ecx
        jnz       d_eight
        mov       r10, r11                        ; carry on with the quotient
        jmp       d_loop
d_tail:
        cmp       r10d, 100
        jb        d_last
        mov       edx, r10d
        imul      rdx, rdx, 51EB851Fh
        shr       rdx, 37
        mov       eax, edx
        imul      eax, eax, 100
        sub       r10d, eax
        movzx     eax, word ptr [r9 + r10*2]
        sub       r8, 2
        mov       word ptr [r8], ax
        mov       r10d, edx
        jmp       d_tail
d_last:
        cmp       r10d, 10
        jb        d_one
        movzx     eax, word ptr [r9 + r10*2]
        sub       r8, 2
        mov       word ptr [r8], ax
        jmp       done
d_one:
        add       r10d, 30h
        dec       r8
        mov       byte ptr [r8], r10b

done:
        xor       eax, eax
        ret
overflow:
        mov       eax, ST_OVERFLOW
        ret
wia_lint2char ENDP

ALIGN 16
wia_li2c_tables PROC
        lea       rax, TB
        ret
wia_li2c_tables ENDP

ALIGN 16
wia_li2c_offsets PROC
        lea       rax, OFFS
        ret
wia_li2c_offsets ENDP

END
