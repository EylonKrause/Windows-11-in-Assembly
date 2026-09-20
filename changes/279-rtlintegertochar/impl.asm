; changes/279-rtlintegertochar/impl.asm
;   NTSTATUS wia_int2char(ULONG value, ULONG base, LONG length, char* out)
;       [Win64: ecx, edx, r8d, r9 -> eax]
;
; ntdll!RtlIntegerToChar. discovery/rtl_integer_char.c measured it at 13.62 ns for ten decimal
; digits and 6.84 for eight hexadecimal, against change 278's 4.28 ns for the same ten.
;
; This is the ANSI, raw-pointer sibling of the export change 278 replaced, and it uses the same
; proved machinery, but its CONTRACT is different in two ways that had to be measured, and one of
; them is a feature nobody would guess.
;
; --------------------------------------------------------------------------------------------------
; 1. `length` Is room in bytes, and the terminator is written only if it fits.
;
;     length  9   ten digits   STATUS_BUFFER_OVERFLOW, buffer untouched
;     length 10   ten digits   SUCCESS, ten characters, NO terminator
;     length 11   ten digits   SUCCESS, ten characters AND a terminator
;
; That is change 067's rule for RtlConvertSidToUnicodeString. It is NOT change 278's rule for
; RtlIntegerToUnicodeString, which demands Length+2 and always writes a terminator. THREE FORMATTERS
; In one DLL, two rules, and the only way to know which is which is to ask each one, one byte at a
; time. probes/contract.c does.
;
; 2. a negative `length` is a zero-padded field width.
;
;     length -9    ten digits   STATUS_BUFFER_OVERFLOW
;     length -10   ten digits   "3735928559"
;     length -11   ten digits   "03735928559"       <-- padded on the left, and NO terminator
;     length -13   ten digits   "0003735928559"
;     length -100               pads to a hundred characters, and FAULTS on a 64-byte buffer
;     length INT_MIN            STATUS_BUFFER_OVERFLOW, it cannot be negated
;
; probes/negative.c found that by sweeping every negative length against a guard page. A positive
; length does NOT pad: value 7 with length 8 is "7" and a terminator, not "00000007". An
; implementation that treated a negative length as an error, or as room, would be wrong on a
; documented feature that a single hand-written test would never have reached.
;
; 3. The bases are change 278's five (0, 2, 8, 10 and 16, with 0 meaning 10) and everything else
;    is STATUS_INVALID_PARAMETER. The value is unsigned. A refusal leaves the buffer untouched.
;
; --------------------------------------------------------------------------------------------------
; 4. This change supersedes change 097, which is wrong on every negative length.
;
; 097 landed this same export at 1.39x. Its capacity test is `cmp edx, r10d / ja overflow`, an
; UNSIGNED compare, so a negative length reads as the largest possible room, and it writes the
; digits left-justified with a terminator where the export writes a zero-padded field. Swept against
; live over every negative length from -1 to -60 in five bases, it differs in 171600 of 171600
; cases, including returning SUCCESS and writing to the buffer on calls the export REFUSES with
; STATUS_BUFFER_OVERFLOW. Its corpus swept `Length 0..40` and drew its random lengths from
; `(s>>7)%40`, so it never asked. (Change 100, the 64-bit sibling, has the same defect: 123000 of
; 123000 negative-length cases differ, and every positive-length case is correct.)
;
; --------------------------------------------------------------------------------------------------
; THE CONVERTERS. Base 10 is change 278's, narrowed to bytes: length-first, two digits at a time
; from a table of pre-packed ASCII pairs, dividing by 100 with a multiply that change 067's
; probes/decimal.c proved exact over ALL 4294967296 values.
;
; The power-of-two bases are NOT 278's shift-and-mask loop. 097 beat that loop on hexadecimal and
; binary by writing them MSB-first with no temp, and a supersession that regressed those rows would
; be the sort of quiet loss these gates exist to catch. So they emit more than one digit per store:
; a byte is two hexadecimal digits, six bits are two octal digits, and a byte is eight binary digits
; that go out as a single 8-byte store. The digit count still comes from a table indexed by
; BSR(base) and the bit length.
;
; ISA: baseline x64, plus SSE2 (baseline on x64) for the zero-padding fill. No YMM is
; touched on any path, so there is no upper state to clear.

OPTION PROC:PRIVATE
PUBLIC wia_int2char
PUBLIC wia_i2c_tables
PUBLIC wia_i2c_tables2

ST_INVALID  EQU 0C000000Dh
ST_OVERFLOW EQU 80000005h

.const
ALIGN 16
; DEC2B[i] is i in decimal, two ASCII bytes, packed little-endian.
DEC2B   LABEL WORD
        NI = 0
        REPT 100
          DW (30h + (NI / 10)) OR ((30h + (NI MOD 10)) SHL 8)
          NI = NI + 1
        ENDM

ALIGN 16
TABS    LABEL QWORD
POW10   LABEL QWORD                          ; +0   : 10^0 .. 10^10
        NP = 1
        REPT 11
          DQ NP
          NP = NP * 10
        ENDM
GTAB    LABEL BYTE                           ; +88  : digits10(2^B)
        NB = 0
        REPT 32
          DB (NB * 1233) / 4096 + 1
          NB = NB + 1
        ENDM
HEXCH   LABEL BYTE                           ; +120 : uppercase
        DB      '0123456789ABCDEF'
ALIGN 16
; The power-of-two bases emit more than one digit per store, for the same reason base 10 does.
; A shift-and-mask loop is one digit per iteration, and change 097, the landed change this one
; supersedes, beat exactly that loop by writing hex and binary MSB-first with no temp. Neither is
; as good as not iterating: a byte of the value is TWO hexadecimal digits, TWO octal digits fit in
; six bits, and a byte is EIGHT binary digits that are one 8-byte store.
TABS2   LABEL BYTE
HEX2    LABEL WORD                           ; +0    : 256 words -- one byte -> two hex characters
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
          DW HC OR (LC SHL 8)                ; high nibble first: it lands at the LOWER address
          NH = NH + 1
        ENDM
OCT2    LABEL WORD                           ; +512  : 64 words -- six bits -> two octal characters
        NO2 = 0
        REPT 64
          DW (30h + (NO2 SHR 3)) OR ((30h + (NO2 AND 7)) SHL 8)
          NO2 = NO2 + 1
        ENDM
BIN8    LABEL BYTE                           ; +640  : 256 rows of 8 -- one byte -> eight characters
        NB8 = 0
        REPT 256
          NK = 7
          REPT 8
            DB 30h + ((NB8 SHR NK) AND 1)
          NK = NK - 1
          ENDM
          NB8 = NB8 + 1
        ENDM

OCTOFF  EQU 512
BINOFF  EQU 640

ALIGN 16
C30     DB      16 DUP(30h)                  ; sixteen ASCII zeros, for the field padding

GOFF    EQU 88
HOFF    EQU 120

.code

; The register budget is seven and the job needs eight, which is why the field width is consumed
; before the table base is loaded. Everything lives in volatile registers; nothing is saved, there
; is no frame and there are no calls:
;
;   r9   the buffer            rax  the write cursor, running backwards from the end of the field
;   r10d the value             rdx  the base, then scratch
;   r11d the digit count, then the mask   rcx  scratch, and the shift for SHR
;   r8d  the field width, then the table base; the width is finished with by then
;
; The first draft loaded the table into r9 and lost the buffer, which the padding loop needs.

ALIGN 16
wia_int2char PROC
        mov       r10d, ecx                       ; the value
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
        ; The digit count is arithmetic, not a table lookup, and that is worth a paragraph.
        ; The first version indexed a table by BSR(base)*32 + BSR(value), correct, and checked
        ; against its definition, but a shift, an add and a LOAD hang off the BSR before the first
        ; character can be written. The A/B against change 097 showed it: eight hexadecimal digits
        ; came out at 0.93x of the change being superseded, which is a regression whichever way it
        ; is measured. 097 sizes its output with LZCNT and arithmetic and pays no load at all.
        ; Here BSR gives the index of the top set bit (one less than the bit length) so the
        ; digit count is that index divided by the shift, plus one. Division by 4 is a shift;
        ; division by 3 is a multiply, and the index is at most 31 so `(n * 0AAABh) >> 17` is
        ; floor(n/3) over the whole domain that can occur. correctness.c re-derives every digit
        ; count the slow way over all 2^32 boundaries, so a wrong constant cannot survive.
        mov       ecx, r10d
        or        ecx, 1                          ; BSR is undefined at zero; "0" is one digit
        bsr       ecx, ecx
        cmp       edx, 2
        je        dp_done                         ; base 2: one digit per bit
        cmp       edx, 16
        jne       dp_oct
        shr       ecx, 2                          ; base 16: one digit per four bits
dp_done:
        lea       r11d, [rcx + 1]
        jmp       have_digits
dp_oct:
        imul      ecx, ecx, 0AAABh                ; base 8: one digit per three bits
        shr       ecx, 17
        lea       r11d, [rcx + 1]
        jmp       have_digits
d_base10:
        lea       rcx, TABS
        mov       eax, r10d
        or        eax, 1
        bsr       eax, eax
        movzx     r11d, byte ptr [rcx + GOFF + rax]
        mov       eax, r10d
        cmp       rax, qword ptr [rcx + r11*8]
        setae     cl
        movzx     ecx, cl
        add       r11d, ecx                       ; digits10(value)

have_digits:
        ; The room rule. a positive length is room, and the terminator is written only if it fits.
        ; A NEGATIVE length is a zero-padded field width, and no terminator is written at all.
        ; probes/contract.c and probes/negative.c measured both, one byte at a time.
        test      r8d, r8d
        jg        pos_len
        jz        overflow                        ; length 0 is a refusal
        cmp       r8d, 80000000h
        je        overflow                        ; INT_MIN cannot be negated
        neg       r8d                             ; the field width
        cmp       r11d, r8d
        ja        overflow
        lea       rax, [r9 + r8]                  ; one past the field; no terminator
        jmp       dispatch
pos_len:
        cmp       r11d, r8d
        ja        overflow
        lea       rax, [r9 + r11]                 ; a positive length never pads: the field IS the
        je        dispatch                        ;   digits, and if it exactly fills there is no
        mov       byte ptr [rax], 0               ;   room for a terminator

dispatch:
        cmp       edx, 10
        je        w_base10
        cmp       edx, 16
        je        w_hex
        cmp       edx, 8
        je        w_oct
        ; base 2 falls through

; ---- base 2: EIGHT digits per store, from a table of 256 eight-byte rows.
;      Thirty-two binary digits is four stores, not thirty-two iterations.
        lea       r8, BIN8
b2_next:
        cmp       r11d, 8
        jb        b2_tail
        movzx     ecx, r10b
        mov       rdx, qword ptr [r8 + rcx*8]
        sub       rax, 8
        mov       qword ptr [rax], rdx
        shr       r10d, 8
        sub       r11d, 8
        jmp       b2_next
b2_tail:
        test      r11d, r11d
        jz        pad
b2_one:
        mov       ecx, r10d
        and       ecx, 1
        add       ecx, 30h
        dec       rax
        mov       byte ptr [rax], cl
        shr       r10d, 1
        dec       r11d
        jnz       b2_one
        jmp       pad

; ---- base 16: TWO digits per store; one byte of the value is exactly two hexadecimal characters
w_hex:
        lea       r8, TABS2
h16_next:
        cmp       r11d, 2
        jb        h16_last
        movzx     ecx, r10b
        movzx     ecx, word ptr [r8 + rcx*2]
        sub       rax, 2
        mov       word ptr [rax], cx
        shr       r10d, 8
        sub       r11d, 2
        jmp       h16_next
h16_last:
        test      r11d, r11d
        jz        pad
        and       r10d, 0Fh
        lea       rcx, TABS
        movzx     ecx, byte ptr [rcx + HOFF + r10]
        dec       rax
        mov       byte ptr [rax], cl
        jmp       pad

; ---- base 8: TWO digits per store, six bits of the value are exactly two octal characters
w_oct:
        lea       r8, TABS2
o8_next:
        cmp       r11d, 2
        jb        o8_last
        mov       ecx, r10d
        and       ecx, 3Fh
        movzx     ecx, word ptr [r8 + OCTOFF + rcx*2]
        sub       rax, 2
        mov       word ptr [rax], cx
        shr       r10d, 6
        sub       r11d, 2
        jmp       o8_next
o8_last:
        test      r11d, r11d
        jz        pad
        and       r10d, 7
        add       r10d, 30h
        dec       rax
        mov       byte ptr [rax], r10b
        jmp       pad

w_base10:
        lea       r8, DEC2B
b10_pairs:
        cmp       r11d, 2
        jb        b10_last
        mov       edx, r10d
        imul      rdx, rdx, 51EB851Fh
        shr       rdx, 37                         ; value / 100, proved exact over all 2^32
        imul      ecx, edx, 100
        sub       r10d, ecx                       ; the remainder, 0..99
        movzx     ecx, word ptr [r8 + r10*2]
        sub       rax, 2
        mov       word ptr [rax], cx              ; two characters, one store
        mov       r10d, edx
        sub       r11d, 2
        jmp       b10_pairs
b10_last:
        test      r11d, r11d
        jz        pad
        add       r10d, 30h
        dec       rax
        mov       byte ptr [rax], r10b

pad:
        ; Anything left between the buffer and the first digit is the zero padding a negative length
        ; asked for. A positive length leaves none, because the field was set to the digit count.
        ;
        ; This is the loop that parked the first bench run. Written a byte at a time it cost 182 ns
        ; to pad a hundred-character field against the export's 115, the only regressing row in a
        ; table of sixteen, and it regressed for the plainest possible reason: ntdll fills the field
        ; with a wide store and this wrote ninety separate bytes. Sixteen at a time, with the two
        ; ends OVERLAPPING so that no count between 16 and 31 needs a loop at all, turned 0.63x into
        ; a win. The whole-field constant is SSE2, which is baseline on x64; no YMM is touched, so
        ; there is no upper state to clear and no VZEROUPPER on any path.
        mov       rcx, rax
        sub       rcx, r9
        jz        done
        mov       rdx, 3030303030303030h
        cmp       rcx, 16
        jb        pad_small
        movups    xmm0, xmmword ptr [C30]
        cmp       rcx, 32
        jb        pad_ends
pad_32:
        movups    xmmword ptr [r9], xmm0
        movups    xmmword ptr [r9 + 16], xmm0
        add       r9, 32
        sub       rcx, 32
        cmp       rcx, 32
        jae       pad_32
        test      rcx, rcx
        jz        done
        cmp       rcx, 16
        jb        pad_small
pad_ends:
        movups    xmmword ptr [r9], xmm0
        movups    xmmword ptr [r9 + rcx - 16], xmm0
        jmp       done
pad_small:
        ; 1..15 bytes, again as two overlapping stores, no loop, no branch per byte
        cmp       rcx, 8
        jb        pad_le7
        mov       qword ptr [r9], rdx
        mov       qword ptr [r9 + rcx - 8], rdx
        jmp       done
pad_le7:
        cmp       rcx, 4
        jb        pad_le3
        mov       dword ptr [r9], edx
        mov       dword ptr [r9 + rcx - 4], edx
        jmp       done
pad_le3:
        ; 1, 2 or 3: the first byte, the last byte, and the middle one, which for a count of 1 or
        ; 2 is one of the other two, written twice with the same value
        mov       byte ptr [r9], dl
        mov       byte ptr [r9 + rcx - 1], dl
        shr       rcx, 1
        mov       byte ptr [r9 + rcx], dl
done:
        xor       eax, eax
        ret
overflow:
        mov       eax, ST_OVERFLOW
        ret
wia_int2char ENDP

ALIGN 16
wia_i2c_tables PROC
        lea       rax, DEC2B
        ret
wia_i2c_tables ENDP

ALIGN 16
wia_i2c_tables2 PROC
        lea       rax, TABS2
        ret
wia_i2c_tables2 ENDP

END
