; combase.dll!IIDFromString  --  hand-written x86-64 reimplementation (3.18x vs shipped)
; source of truth: changes/207-iidfromstring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
;; changes/207-iidfromstring/impl.asm
; HRESULT wia_iidfromstring(const wchar_t* lpsz, GUID* lpiid)   [rcx, rdx -> eax]
;
; Reimplements combase!IIDFromString -- 32.96 ns to parse 38 characters. Two things make it that
; expensive, and both are visible at RVA 0x000E6C20:
;   * the length check is a one-character-at-a-time strlen -- 38 iterations of a two-instruction
;     dependent loop before any parsing starts;
;   * each hex digit costs three range compares (0-9, A-F, a-f) plus a shift, an add and a store.
;
; THE FAILURE CONTRACT IS THE HARD PART, AND IT WAS MEASURED, NOT GUESSED. This function writes into
; the caller's GUID AS IT PARSES, so a malformed string leaves a partially-filled GUID that has to be
; reproduced byte for byte. probes/wmask.c corrupts exactly one character at a time and reports which
; of the sixteen output bytes moved away from a poison fill. The table that came back:
;
;     corrupted char   bytes written        because
;     0   '{'          none                 the brace is checked before anything is stored
;     1..8             0-3, PARTIAL Data1   Data1 is zeroed first, then re-stored after EVERY digit
;     9   '-'          0-3, full Data1
;     10..13           0-3                  Data2 is not stored yet
;     14  '-'          0-3                  ...not even after its four digits validate
;     15..18           0-5                  Data2 was stored once its separator validated
;     19  '-'          0-5
;     20..21           0-7                  Data3 stored after ITS separator
;     22..23           0-8                  Data4[0] stored right after its two digits (no separator)
;     24  '-'          0-8                  Data4[1] waits for the separator at 24
;     25..36           one more byte per hex pair
;     37  '}'          all 16               Data4[7] is stored BEFORE the brace is checked
;
; So every field is stored only once IT AND ITS TRAILING SEPARATOR have validated -- except Data1,
; which is progressive, and Data4[0], which has no trailing separator. An implementation that simply
; parsed into a scratch and stored on success would pass a return-value test and fail this one.
;
; TWO DIFFERENT ERROR CODES, and they are not interchangeable:
;   * 80070057h (E_INVALIDARG) -- lpiid is NULL, or the string length is not EXACTLY 38. Nothing is
;     written. This is a structural rejection, decided before the parser runs.
;   * 800401F4h (CO_E_IIDSTRING) -- the length was right but the content is not. Partial writes as
;     tabulated above.
; The shipped code produces the second with `neg eax / sbb eax,eax / not eax / and eax, 800401F4h`
; from the inner parser's boolean, which is why a content failure can never return E_INVALIDARG.
;
; lpsz == NULL is SUCCESS: it writes the nil GUID and returns S_OK.
;
; WHAT WE DO INSTEAD:
;   * the length check is one AVX2 pass -- two vpcmpeqw over chars 0..31 plus a 16-byte tail -- so
;     the 38-iteration walk disappears;
;   * every digit is one 256-entry table lookup instead of three range compares. Data1 keeps a branch
;     per digit because its partial value depends on WHICH digit failed; every other field
;     accumulates branchlessly and is tested once, since a failure anywhere in a field means the
;     whole field goes unwritten.
;
; PAGE SAFETY: 39 characters plus a 16-byte tail load is 80 bytes read before the length is known.
; Within 80 bytes of a page boundary the code falls back to the bounded character walk, which stops
; at the first NUL. Same discipline as changes 001-004 and 205.
;
; ISA: AVX2 for the length check; the parse is baseline x64.

.const
ALIGN 16
; hex character -> nibble, 0FFh for everything else ('-', '{', '}' and NUL included)
hexval:
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 00h,01h,02h,03h,04h,05h,06h,07h,08h,09h,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh

.code
; --- one Data1 digit: branch on failure, because Data1's partial value depends on which digit it was
D1 MACRO ci
        movzx     eax, word ptr [rcx + ci*2]
        cmp       eax, 127
        ja        f_content
        movzx     eax, byte ptr [r8 + rax]
        cmp       al, 0FFh
        je        f_content
        shl       r9d, 4
        or        r9d, eax
        mov       dword ptr [rdx], r9d         ; progressive, exactly like the shipped code
        ENDM

; --- one digit of any other field: branchless. The high byte lands in bits 8+ of the validity
; --- accumulator and the table's 0FFh in bits 4..7, so neither can be mistaken for a real nibble.
HD MACRO ci
        movzx     eax, word ptr [rcx + ci*2]
        mov       r11d, eax
        and       r11d, 0FF00h
        or        r10d, r11d
        movzx     eax, al
        movzx     eax, byte ptr [r8 + rax]
        or        r10d, eax
        shl       r9d, 4
        or        r9d, eax
        ENDM

; --- a separator or brace at a fixed position
LIT MACRO ci, ch
        cmp       word ptr [rcx + ci*2], ch
        jne       f_content
        ENDM

wia_iidfromstring PROC
        test      rdx, rdx
        jz        f_inval                      ; lpiid NULL -> E_INVALIDARG, nothing written
        test      rcx, rcx
        jz        nil_guid                     ; lpsz NULL -> nil GUID, S_OK

        ;================ the length must be EXACTLY 38 characters ================
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4096 - 80
        ja        len_walk                     ; too close to a page end to read 80 bytes blind

        vpxor     ymm2, ymm2, ymm2
        vmovdqu   ymm0, ymmword ptr [rcx]      ; chars 0..15
        vmovdqu   ymm1, ymmword ptr [rcx + 32] ; chars 16..31
        vpcmpeqw  ymm0, ymm0, ymm2
        vpcmpeqw  ymm1, ymm1, ymm2
        vpor      ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        vzeroupper
        test      eax, eax
        jnz       f_inval                      ; a NUL inside chars 0..31 -> shorter than 38

        vmovdqu   xmm0, xmmword ptr [rcx + 64] ; chars 32..39
        vpxor     xmm1, xmm1, xmm1
        vpcmpeqw  xmm0, xmm0, xmm1
        vpmovmskb eax, xmm0
        test      eax, 0FFFh                   ; chars 32..37 must all be non-NUL
        jnz       f_inval
        test      eax, 3000h                   ; char 38 must BE the NUL
        jz        f_inval

len_ok:
        lea       r8, hexval
        xor       r9d, r9d
        xor       r10d, r10d

        ;================ '{' then Data1, chars 1..8 ================
        LIT       0, '{'
        mov       dword ptr [rdx], r9d         ; Data1 is zeroed BEFORE its first digit, which is why
                                               ;   a failure on digit 1 leaves Data1 == 0
        D1        1
        D1        2
        D1        3
        D1        4
        D1        5
        D1        6
        D1        7
        D1        8

        ;================ Data2, chars 10..13 -- stored only after the separator at 14 ============
        LIT       9, '-'
        xor       r9d, r9d
        HD        10
        HD        11
        HD        12
        HD        13
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        LIT       14, '-'                      ; Data2 is still unwritten if THIS fails
        mov       word ptr [rdx + 4], r9w

        ;================ Data3, chars 15..18 -- stored only after the separator at 19 ============
        xor       r9d, r9d
        HD        15
        HD        16
        HD        17
        HD        18
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        LIT       19, '-'
        mov       word ptr [rdx + 6], r9w

        ;================ Data4[0], chars 20..21 -- no trailing separator, stored at once =========
        xor       r9d, r9d
        HD        20
        HD        21
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        mov       byte ptr [rdx + 8], r9b

        ;================ Data4[1], chars 22..23 -- waits for the separator at 24 ================
        xor       r9d, r9d
        HD        22
        HD        23
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        LIT       24, '-'
        mov       byte ptr [rdx + 9], r9b

        ;================ Data4[2], chars 25..26 ================
        xor       r9d, r9d
        HD        25
        HD        26
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        mov       byte ptr [rdx + 10], r9b

        ;================ Data4[3], chars 27..28 ================
        xor       r9d, r9d
        HD        27
        HD        28
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        mov       byte ptr [rdx + 11], r9b

        ;================ Data4[4], chars 29..30 ================
        xor       r9d, r9d
        HD        29
        HD        30
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        mov       byte ptr [rdx + 12], r9b

        ;================ Data4[5], chars 31..32 ================
        xor       r9d, r9d
        HD        31
        HD        32
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        mov       byte ptr [rdx + 13], r9b

        ;================ Data4[6], chars 33..34 ================
        xor       r9d, r9d
        HD        33
        HD        34
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        mov       byte ptr [rdx + 14], r9b

        ;================ Data4[7], chars 35..36 ================
        xor       r9d, r9d
        HD        35
        HD        36
        test      r10d, 0FFFFFFF0h
        jnz       f_content
        mov       byte ptr [rdx + 15], r9b

        ;================ the closing brace is checked LAST, after all 16 bytes are stored ========
        LIT       37, '}'
        xor       eax, eax                     ; S_OK
        ret

        ; ---- within 80 bytes of a page boundary: walk for the terminator, bounded, then parse ----
len_walk:
        xor       eax, eax
lw_scan:
        cmp       word ptr [rcx + rax*2], 0
        je        lw_found
        inc       eax
        cmp       eax, 39
        jb        lw_scan
lw_found:
        cmp       eax, 38
        jne       f_inval
        jmp       len_ok

nil_guid:
        xor       eax, eax
        mov       qword ptr [rdx], rax
        mov       qword ptr [rdx + 8], rax
        ret                                    ; eax is already 0 = S_OK

f_inval:
        mov       eax, 80070057h               ; E_INVALIDARG -- structural, nothing written
        ret
f_content:
        mov       eax, 800401F4h               ; CO_E_IIDSTRING -- length was right, content was not
        ret
wia_iidfromstring ENDP
END
