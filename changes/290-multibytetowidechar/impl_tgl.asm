; changes/290-multibytetowidechar/impl_tgl.asm: TGL VARIANT, bench #3 only
;
; Intel Core i9-11900H (Tiger Lake-H).  impl.asm is UNTOUCHED and remains the implementation of
; record; this file exists because that one is PARKED, and it is parked for exactly one reason.
;
; What is wrong with the parent, in its own words: it keeps change 034's homogeneous blocks and
; adds `gen16`, which decodes SIXTEEN bytes of arbitrary UTF-8 in one pass.  That lifted the
; mixed-width classes a long way (from change 034's 0.29x-0.59x to 0.79x-0.97x) and still did
; not reach parity.  On bench #3 the parent measures geomean 1.780x and PARKS, and every regressing
; row is mixed-width: a+3, a+4, 2+3, 1234 and their measuring-mode twins.
;
; What this file changes, and nothing else: it puts a sixty-four byte width-agnostic block in front
; of `gen16`, lifted from changes/034-rtlutf8tounicoden/impl_tgl.asm, which is the same decoder the
; README's 290 row asks for by name.  That decoder took change 034's identical classes from
; 0.29x-0.59x to 1.47x-2.14x, so the prior is measured rather than hoped for.  `gen16` is kept
; verbatim as `gen16_small` and still handles everything under 64 bytes, exactly as 034's variant
; keeps its own 16- and 8-byte blocks behind the 64-byte one.
;
; Everything else is the parent's: the dispatch boundary and its proof, the measuring mode, the
; NUL-terminated form, the per-unit overflow rule, the maximal-subpart rule, the scalar walk and
; the whole wrapper.  Those are what correctness.c gates and none of them move.
;
; THREE ADAPTATIONS were needed, and they are the only edits to the lifted code:
;   1. Change 034 uses r15 as both the unit counter and the store index.  This file does not: r15
;      counts and r12 stores, because r12 is ANDed with F_STOREMASK so that the measuring mode
;      writes to a scratch buffer instead of the caller's.  Every store became [rbx + r12*2] and
;      every advance of r15 is twinned with an advance of r12 and the mask.
;   2. F_SCRATCH grew from 64 Bytes to 128, and the parent's invariant comment with it.  The parent
;      states "64 bytes is the widest any block writes", which was true of a 32-byte ASCII block
;      and is NOT true of a 64-byte one: 64 ASCII bytes are 64 UTF-16 units, which is 128 bytes.
;      Leaving it at 64 would have let the measuring mode overwrite its own frame.
;   3. `mix16`, change 034's next-smaller block, becomes `gen16_small`, this file's.
;
; ISA: AVX-512 F/BW/DQ/VL/VBMI/VBMI2 + BMI1/BMI2 + POPCNT.  Bench #1 and bench #2 cannot run it,
; which is why it is a variant and not an edit.
;
        OPTION  PROC:PRIVATE
        PUBLIC  wia_mbtwc
        PUBLIC  wia_mbtwc_set_fallback
        PUBLIC  wia_mbtwc_mix_table
        PUBLIC  wia_mbtwc_lead_table
        PUBLIC  wia_mbtwc_store_masks

CP_UTF8_        EQU     0FDE9h
E_PARAM         EQU     057h                    ; ERROR_INVALID_PARAMETER
E_BUF           EQU     07Ah                    ; ERROR_INSUFFICIENT_BUFFER
E_FLAGS         EQU     3ECh                    ; ERROR_INVALID_FLAGS
E_NOXLAT        EQU     459h                    ; ERROR_NO_UNICODE_TRANSLATION

; the stack frame, all of it
F_NOTMAP        EQU     0                       ; dword: something was substituted
F_OVER          EQU     4                       ; dword: the destination filled up
F_WINDOW        EQU     8                       ; dword: decode scalar this far before probing again
F_STOREMASK     EQU     16                      ; qword: -1 converting, 0 measuring (see below)
F_FLAGS         EQU     24                      ; dword: dwFlags
F_SCRATCH       EQU     32                      ; 128 bytes: where the measuring mode's stores land
F_SIZE          EQU     168   ; was 104; the scratch grew by 64 for gen64

        .const
        ALIGN 16
; --- byte domain: the same constant is used both as an AND mask and as a compare value ---
BC0     DB      16 dup(0C0h)
B80     DB      16 dup(080h)
BE0     DB      16 dup(0E0h)
BF0     DB      16 dup(0F0h)
BF8     DB      16 dup(0F8h)
BFE     DB      16 dup(0FEh)
BED     DB      16 dup(0EDh)
BF4     DB      16 dup(0F4h)
B1F     DB      16 dup(01Fh)                    ; biased: b1 > 0x9F
B0F     DB      16 dup(00Fh)                    ; biased: b1 > 0x8F
B74     DB      16 dup(074h)                    ; biased: b0 > 0xF4
; --- word domain (one input byte per 16-bit lane) ---
        ALIGN 16
W001F   DW      16 dup(001Fh)
W003F   DW      16 dup(003Fh)
W000F   DW      16 dup(000Fh)
W0007   DW      16 dup(0007h)
W0003   DW      16 dup(0003h)
W00E0   DW      16 dup(00E0h)
W00C0   DW      16 dup(00C0h)
W00F0   DW      16 dup(00F0h)
W00F8   DW      16 dup(00F8h)
WD7C0   DW      16 dup(0D7C0h)
WDC00   DW      16 dup(0DC00h)
; --- change 034's homogeneous blocks use these unchanged ---
        ALIGN 16
CQMASKw DW      8 dup(0C0E0h)
CQWANTw DW      8 dup(080C0h)
CQ1Ew   DW      16 dup(001Eh)
CQDECw  DW      8 dup(3F1Fh)
CQMULb  DB      8 dup(40h, 01h)
CQ3SHUF DB      0,1,2,80h, 3,4,5,80h, 6,7,8,80h, 9,10,11,80h
        DB      0,1,2,80h, 3,4,5,80h, 6,7,8,80h, 9,10,11,80h
CQ3MSKd DD      8 dup(00C0C0F0h)
CQ3WNTd DD      8 dup(008080E0h)
CQ0Fd   DD      8 dup(0000000Fh)
CQ0Dd   DD      8 dup(0000000Dh)
CQ3Fd   DD      8 dup(0000003Fh)
CQ4MSKd DD      4 dup(0C0C0C0FCh)
CQ4WNTd DD      4 dup(808080F0h)
CQ03d   DD      4 dup(00000003h)
CQ30d   DD      4 dup(00003000h)
CQ07d   DD      4 dup(00000007h)
CQ3FFd  DD      4 dup(000003FFh)
CQ10000d DD     4 dup(00010000h)
CQD800d DD      4 dup(0000D800h)
CQDC00d DD      4 dup(0000DC00h)

; -------------------------------------------------------------------------------------------------
; The compaction table, generated by the assembler rather than pasted in.
;
; gen16 computes one UTF-16 unit per INPUT POSITION and then throws away the positions that carry
; no unit of their own, the continuation bytes.  Which positions survive varies, so the only
; thing that varies is the shuffle: 256 of them, one per combination of which of eight positions
; are KEPT.  correctness.c checks every entry against the same rule written in C.
; -------------------------------------------------------------------------------------------------
        ALIGN 16
MIXTAB LABEL BYTE
PKI = 0
REPT 256
    PKN = 0
    PKJ = 0
    REPT 8
        IF (PKI SHR PKJ) AND 1
            DB (2*PKJ)
            DB (2*PKJ)+1
            PKN = PKN + 2
        ENDIF
        PKJ = PKJ + 1
    ENDM
    REPT 16 - PKN
        DB 80h
    ENDM
    PKI = PKI + 1
ENDM

; The nine store masks the compaction needs: entry k keeps the first k words and leaves the rest
; of the destination exactly as the caller left it.  correctness.c checks all nine.
MASKTAB LABEL BYTE
MKI = 0
REPT 9
    MKJ = 0
    REPT 8
        IF MKJ LT MKI
            DW 0FFFFh
        ELSE
            DW 0
        ENDIF
        MKJ = MKJ + 1
    ENDM
    MKI = MKI + 1
ENDM
MTOFF   EQU     MASKTAB - MIXTAB

; -------------------------------------------------------------------------------------------------
; The lead table, also generated.  One dword per possible lead byte:
;
;       bits  0.. 7   the expected sequence length, or 0 for a byte that is never a lead
;       bits  8..15   the lead's own lower bound for byte 2, BIASED BY 0x80
;       bits 16..23   the span of that bound (hi - lo)
;       bits 24..31   the lead's payload bits, already masked
;
; This replaces two compare cascades in the scalar walk, one to derive the length, one to derive
; the byte-2 range, with a single load.  discovery/utf8_width_mixtures.c named those two cascades
; as where change 034's time actually went on mixed-width input.
; -------------------------------------------------------------------------------------------------
        ALIGN 16
UTAB LABEL DWORD
UI = 0
REPT 256
    IF UI LT 0C2h
        DD 0
    ELSEIF UI LT 0E0h
        DD 2 + (0 SHL 8) + (3Fh SHL 16) + ((UI AND 1Fh) SHL 24)
    ELSEIF UI EQ 0E0h
        DD 3 + (20h SHL 8) + (1Fh SHL 16) + (0 SHL 24)
    ELSEIF UI EQ 0EDh
        DD 3 + (0 SHL 8) + (1Fh SHL 16) + (0Dh SHL 24)
    ELSEIF UI LT 0F0h
        DD 3 + (0 SHL 8) + (3Fh SHL 16) + ((UI AND 0Fh) SHL 24)
    ELSEIF UI EQ 0F0h
        DD 4 + (10h SHL 8) + (2Fh SHL 16) + (0 SHL 24)
    ELSEIF UI EQ 0F4h
        DD 4 + (0 SHL 8) + (0Fh SHL 16) + (4 SHL 24)
    ELSEIF UI LT 0F4h
        DD 4 + (0 SHL 8) + (3Fh SHL 16) + ((UI AND 7) SHL 24)
    ELSE
        DD 0
    ENDIF
    UI = UI + 1
ENDM

        .data
        ALIGN 8
        PUBLIC  wia_mbtwc_fallback
wia_mbtwc_fallback  QWORD 0                     ; the real MultiByteToWideChar

; ===== lifted from changes/034-rtlutf8tounicoden/impl_tgl.asm =====
WIAK64 SEGMENT ALIGN(64) READONLY 'CONST'
GC0     DB      64 dup(0C0h)                      ; signed < -64 is exactly 0x80..0xBF
GNIB    DB      64 dup(0Fh)
GID     LABEL   BYTE                              ; 0..63   -- the position of each byte
GI = 0
REPT 64
    DB  GI
    GI = GI + 1
ENDM
GID1    LABEL   BYTE                              ; 1..64   -- and the position after it
GI = 1
REPT 64
    DB  GI
    GI = GI + 1
ENDM
GREPA   LABEL   BYTE                              ; character j of 0..15 -> its four byte lanes
GJ = 0
REPT 16
    DB  GJ, GJ, GJ, GJ
    GJ = GJ + 1
ENDM
GSUB4   DB      16 dup(1,2,3,4)                   ; gather index = p(j+1) - 1 - k
GK4     DB      16 dup(0,1,2,3)
; the low nibble of a byte, which error classes it COULD be
;   bit0/bit2 E0-or-F0 (low nibble 0) | bit1 ED | bit3 F4 | bit4 C0/C1 | bit5 F5..FF
GTLO    DB      15h,10h,00h,00h,08h,20h,20h,20h,20h,20h,20h,20h,20h,22h,20h,20h
        DB      48 dup(0)
; the high nibble, which of those classes this byte is actually in
GTHI    DB      00h,00h,00h,00h,00h,00h,00h,00h,00h,00h,00h,00h,10h,00h,03h,2Ch
        DB      48 dup(0)
; the NEXT byte's high nibble, whether that second byte is out of the lead's narrow range.
; bits 4 and 5 are set everywhere so the C0/C1 and F5..FF verdicts pass through unchanged.
GTB2    DB      30h,30h,30h,30h,30h,30h,30h,30h,35h,39h,3Ah,3Ah,30h,30h,30h,30h
        DB      48 dup(0)
; the length a lead asks for, from its high nibble (8..B is a continuation and asks for none)
GEXP    DB      1,1,1,1,1,1,1,1,0,0,0,0,2,2,3,4
        DB      48 dup(0)
; the payload mask of a RIGHT-ALIGNED sequence, indexed by 4*L + k
GMTAB   DB      000h,000h,000h,000h               ; L=0 -- a lane past the end of the block
        DB      0FFh,000h,000h,000h               ; L=1  b0
        DB      0FFh,01Fh,000h,000h               ; L=2  b1 b0
        DB      0FFh,03Fh,00Fh,000h               ; L=3  b2 b1 b0
        DB      0FFh,03Fh,03Fh,007h               ; L=4  b3 b2 b1 b0
        DB      44 dup(0)
G7F     DD      16 dup(0000007Fh)
G0FC0   DD      16 dup(00000FC0h)
G3F000  DD      16 dup(0003F000h)
G1C0000 DD      16 dup(001C0000h)
GC2     DB      64 dup(0C2h)
GF5     DB      64 dup(0F5h)
GFFFDW  DW      32 dup(0FFFDh)
G16     DB      64 dup(16)
GE0     DB      64 dup(0E0h)
GF0     DB      64 dup(0F0h)
G30     DB      64 dup(30h)   ; the verdict bits that mean "this byte can never lead"
G0F     DB      64 dup(0Fh)   ; ... and the ones that mean "its second byte is out of range"
GFFFD   DD      16 dup(0000FFFDh)
GFFFF   DD      16 dup(0000FFFFh)
G10000  DD      16 dup(00010000h)
GD800   DD      16 dup(0000D800h)
G3FF    DD      16 dup(000003FFh)
GDC00   DD      16 dup(0000DC00h)
WIAK64 ENDS

        .code
; sixteen characters: gather, decode, split the surrogates, and store exactly what came out.
; RIDX says which sixteen (it starts at GREPA = 0..15 and steps by sixteen); r9d is how many
; of them are real.
GENHALF MACRO RIDX
        vpermb    zmm20, RIDX, zmm4                 ; p(j+1), replicated over four byte lanes
        vpsubb    zmm20, zmm20, zmmword ptr [GSUB4] ; the four gather indices of character j
        vpermb    zmm21, zmm20, zmm0                ; the sequence, right-aligned in its dword
        vpermb    zmm22, RIDX, zmm18                ; its length, replicated the same way
        vpaddb    zmm22, zmm22, zmm22
        vpaddb    zmm22, zmm22, zmm22               ; 4*L  (byte adds: nothing crosses a lane)
        vpaddb    zmm22, zmm22, zmmword ptr [GK4]   ; 4*L + k
        vpermb    zmm22, zmm22, zmmword ptr [GMTAB]
        vpandd    zmm21, zmm21, zmm22               ; only the payload bits of each byte survive
        vpsrld    zmm23, zmm21, 2
        vpsrld    zmm24, zmm21, 4
        vpsrld    zmm25, zmm21, 6
        vpandd    zmm21, zmm21, zmmword ptr [G7F]
        vpandd    zmm23, zmm23, zmmword ptr [G0FC0]
        vpandd    zmm24, zmm24, zmmword ptr [G3F000]
        vpandd    zmm25, zmm25, zmmword ptr [G1C0000]
        vpternlogd zmm21, zmm23, zmm24, 0FEh        ; A or B or C
        vpord     zmm21, zmm21, zmm25               ; the code point, all four widths at once
        vmovdqu32 zmm21{k7}, zmmword ptr [GFFFD]  ; ... unless the subpart was malformed
        vpcmpud   k5, zmm21, zmmword ptr [GFFFF], 6 ; above the BMP: it needs a surrogate pair
        vpsubd    zmm23, zmm21, zmmword ptr [G10000]
        vpsrld    zmm24, zmm23, 10
        vpaddd    zmm24, zmm24, zmmword ptr [GD800] ; the high surrogate
        vpandd    zmm23, zmm23, zmmword ptr [G3FF]
        vpaddd    zmm23, zmm23, zmmword ptr [GDC00]
        vpslld    zmm23, zmm23, 16                  ; the low surrogate, above it in the same lane
        vpord     zmm23, zmm23, zmm24
        vmovdqa32 zmm21{k5}, zmm23
        kmovw     eax, k5
        mov       ecx, r9d
        ; a lane past the last character is not silent. `p(j)` comes from the lead mask and `p(j+1)`
        ; from the END mask, and the lead mask has one more bit set than the end mask, so the lane
        ; after the last complete character gets p(j+1)=0 and p(j)=the last lead, i.e. a NEGATIVE
        ; length, which lands on the four-byte entry of the width table and decodes four real bytes
        ; into a real surrogate pair. It was writing one extra unit per block. The even slots were
        ; already limited to the characters that exist; the odd ones have to be too.
        bzhi      eax, eax, ecx
        add       ecx, ecx
        mov       r11d, -1
        bzhi      r11d, r11d, ecx
        and       r11d, 55555555h                   ; one word for each real character ...
        mov       ecx, 0AAAAAAAAh
        pdep      edx, eax, ecx
        or        r11d, edx                         ; ... and a second one for each pair
        kmovd     k6, r11d
        popcnt    eax, eax
        add       eax, r9d                          ; the units this half produced
        vpcompressw zmmword ptr [rbx + r12*2]{k6}, zmm21
        add       r15, rax
        add       r12, rax                          ; the STORE index tracks it ...
        and       r12, qword ptr [rsp + F_STOREMASK] ; ... and is pinned when measuring
ENDM

wia_mbtwc_mix_table PROC
        lea     rax, [MIXTAB]
        ret
wia_mbtwc_mix_table ENDP

wia_mbtwc_lead_table PROC
        lea     rax, [UTAB]
        ret
wia_mbtwc_lead_table ENDP

wia_mbtwc_store_masks PROC
        lea     rax, [MASKTAB]
        ret
wia_mbtwc_store_masks ENDP

wia_mbtwc_set_fallback PROC
        mov     qword ptr [wia_mbtwc_fallback], rcx
        ret
wia_mbtwc_set_fallback ENDP

; =================================================================================================
wia_mbtwc PROC
        cmp     ecx, CP_UTF8_
        jne     delegate                        ; every other code page is the shipped export's

; ---- parameter validation, still a leaf: nothing is pushed until this passes -------------------
; The order is the shipped order, and the order is observable: a bad flag together with a bad
; parameter reports the PARAMETER.
        mov     r10, qword ptr [rsp + 28h]      ; lpWideCharStr
        mov     r11d, dword ptr [rsp + 30h]     ; cchWideChar
        test    r9d, r9d
        jz      err_param                       ; cbMultiByte == 0
        test    r11d, r11d
        js      err_param                       ; cchWideChar < 0
        test    r8, r8
        jz      err_param                       ; lpMultiByteStr == NULL
        test    r11d, r11d
        jz      check_flags                     ; cchWideChar == 0: neither test below applies
        test    r10, r10
        jz      err_param                       ; a destination is required when cch != 0
        cmp     r8, r10
        je      err_param                       ; ... and it may not BE the source
check_flags:
        test    edx, 0FFFFFFF0h                 ; `and ~7` + `test ~8` == this
        jnz     err_flags

        push    rbx
        push    rsi
        push    rdi
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, F_SIZE
        mov     dword ptr [rsp + F_NOTMAP], 0
        mov     dword ptr [rsp + F_OVER], 0
        mov     dword ptr [rsp + F_FLAGS], edx
        mov     rsi, r8                         ; src
        movsxd  r13, r9d                        ; cbMultiByte (negative = NUL-terminated)

; ---- The measuring mode is the same loop ------------------------------------------------------
; cchWideChar == 0 asks for the size and must write nothing.  Rather than carry a second decoder
; (which is how the counting rule and the conversion rule drift apart) the destination becomes
; a 64-byte scratch on this frame and the STORE INDEX is ANDed with a mask that is 0 in measuring
; mode and -1 otherwise.  Every store then lands on the scratch, the unit counter r15 is unaffected,
; and there is exactly one decoder to get right.  128 bytes is the widest any block writes --
; gen64 turns 64 ASCII bytes into 64 UTF-16 units, and the parent's 64-byte scratch was sized
; for a 32-byte block.  Growing it is edit (2) in this file's header.
        test    r11d, r11d
        jnz     convert_mode
        lea     rbx, [rsp + F_SCRATCH]
        mov     rdi, 7FFFFFFFh                  ; a capacity nothing can reach
        mov     qword ptr [rsp + F_STOREMASK], 0
        jmp     have_dest
convert_mode:
        mov     rbx, r10
        movsxd  rdi, r11d
        mov     qword ptr [rsp + F_STOREMASK], -1
have_dest:

; ---- a negative count means NUL-terminated; the shipped code walks it one byte at a time -------
        test    r13, r13
        jns     have_len
        mov     rax, rsi
        and     rax, -32
        mov     ecx, esi
        and     ecx, 31                         ; how far into the aligned block the string starts
        vpxor   ymm1, ymm1, ymm1
        vmovdqa ymm0, ymmword ptr [rax]         ; ALIGNED: cannot cross into an unmapped page
        vpcmpeqb ymm0, ymm0, ymm1
        vpmovmskb edx, ymm0
        shr     edx, cl                         ; drop the bytes before the string
        test    edx, edx
        jnz     sl_first
        mov     r13, 32
        sub     r13, rcx
sl_loop:
        add     rax, 32
        vmovdqa ymm0, ymmword ptr [rax]
        vpcmpeqb ymm0, ymm0, ymm1
        vpmovmskb edx, ymm0
        test    edx, edx
        jnz     sl_found
        add     r13, 32
        jmp     sl_loop
sl_found:
        tzcnt   edx, edx
        add     r13, rdx
        jmp     sl_end
sl_first:
        tzcnt   edx, edx
        mov     r13d, edx
sl_end:
        inc     r13                             ; the terminator is converted too
        cmp     r13, 7FFFFFFFh
        ja      err_param_late
        mov     edx, dword ptr [rsp + F_FLAGS]

have_len:
        lea     rbp, [UTAB]                     ; the scalar walk's lead table, kept in a register
        xor     r14, r14                        ; i, the source index
        xor     r15, r15                        ; o, UTF-16 units produced
        xor     r12, r12                        ; the store index (== o, or pinned to 0)

; -------------------------------------------------------------------------------------------------
; The dispatch.  One byte decides, because that byte is the next character's lead and the blocks
; below are mutually exclusive.  Chaining them instead makes whichever block finally matches pay
; for every earlier one, change 034 measured that at about a third of the mixed class's total.
; -------------------------------------------------------------------------------------------------
ALIGN 16
mainloop:
        cmp     r14, r13
        jae     done
        movzx   eax, byte ptr [rsi + r14]
        cmp     eax, 80h
        jb      ascii32
        cmp     eax, 0C2h
        jb      scalar_win                      ; a stray continuation, or 0xC0 / 0xC1
        cmp     eax, 0E0h
        jb      two8
        cmp     eax, 0F0h
        jb      three8
        cmp     eax, 0F5h
        jb      four8
        jmp     scalar_win                      ; 0xF5..0xFF is never a lead

; ---- ASCII: 32 bytes in, 64 out ---------------------------------------------------------------
ALIGN 16
ascii32:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 32
        jb      ascii16
        lea     rax, [r15 + 32]
        cmp     rax, rdi
        ja      ascii16
        vmovdqu ymm0, ymmword ptr [rsi + r14]
        vpmovmskb eax, ymm0
        test    eax, eax
        jnz     ascii16
        cmp     qword ptr [rsp + F_STOREMASK], 0
        je      a32_count
        vpmovzxbw ymm1, xmm0
        vmovdqu ymmword ptr [rbx + r12*2], ymm1
        vextracti128 xmm0, ymm0, 1
        vpmovzxbw ymm1, xmm0
        vmovdqu ymmword ptr [rbx + r12*2 + 32], ymm1
a32_count:
        add     r14, 32
        add     r15, 32
        add     r12, 32
        and     r12, qword ptr [rsp + F_STOREMASK]
        jmp     mainloop

ALIGN 16
ascii16:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 16
        jb      ascii8
        lea     rax, [r15 + 16]
        cmp     rax, rdi
        ja      ascii8
        vmovdqu xmm0, xmmword ptr [rsi + r14]
        vpmovmskb eax, xmm0
        test    eax, eax
        jnz     ascii8
        cmp     qword ptr [rsp + F_STOREMASK], 0
        je      a16_count
        vpmovzxbw ymm1, xmm0
        vmovdqu ymmword ptr [rbx + r12*2], ymm1
a16_count:
        add     r14, 16
        add     r15, 16
        add     r12, 16
        and     r12, qword ptr [rsp + F_STOREMASK]
        jmp     mainloop

ALIGN 16
ascii8:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 8
        jb      gen64
        lea     rax, [r15 + 8]
        cmp     rax, rdi
        ja      gen64                           ; NOT the scalar path: every block tests its own
        vmovq   xmm0, qword ptr [rsi + r14]     ; room, so that no guard below becomes untestable
        vpmovmskb eax, xmm0
        and     eax, 0FFh
        jnz     gen64
        cmp     qword ptr [rsp + F_STOREMASK], 0
        je      a8_count
        vpmovzxbw xmm1, xmm0
        vmovdqu xmmword ptr [rbx + r12*2], xmm1
a8_count:
        add     r14, 8
        add     r15, 8
        add     r12, 8
        and     r12, qword ptr [rsp + F_STOREMASK]
        jmp     mainloop

; -------------------------------------------------------------------------------------------------
; The two-byte block (change 034): sixteen bytes, eight characters, no shuffle.
; Viewed as 16-bit words the input is [lead, continuation] per character, so ANDing with 0xC0E0 and
; comparing against 0x80C0 tests the whole structure of a two-byte run in one VPCMPEQW; the second
; comparison excludes 0xC0/0xC1.  VPMADDUBSW by [0x40,0x01] IS (lead & 0x1F)*64 + (cont & 0x3F).
; -------------------------------------------------------------------------------------------------
ALIGN 16
two8:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 16
        jb      scalar_win
        lea     rax, [r15 + 8]
        cmp     rax, rdi
        ja      scalar_win
        vmovdqu xmm0, xmmword ptr [rsi + r14]
        vpand   xmm1, xmm0, xmmword ptr [CQMASKw]
        vpcmpeqw xmm1, xmm1, xmmword ptr [CQWANTw]
        vpmovmskb eax, xmm1
        cmp     eax, 0FFFFh
        jne     gen64                           ; not eight clean two-byte sequences
        vpand   xmm1, xmm0, xmmword ptr [CQ1Ew]
        vpxor   xmm2, xmm2, xmm2
        vpcmpeqw xmm1, xmm1, xmm2
        vpmovmskb eax, xmm1
        test    eax, eax
        jnz     gen64                           ; a 0xC0 or 0xC1 lead: overlong, substituted
        vpand   xmm1, xmm0, xmmword ptr [CQDECw]
        vpmaddubsw xmm1, xmm1, xmmword ptr [CQMULb]
        vmovdqu xmmword ptr [rbx + r12*2], xmm1
        add     r14, 16
        add     r15, 8
        add     r12, 8
        and     r12, qword ptr [rsp + F_STOREMASK]
        jmp     mainloop

; -------------------------------------------------------------------------------------------------
; The three-byte block (change 034): twenty-four bytes consumed, twenty-eight read.
; Three divides neither 16 nor 32, so one VPSHUFB spreads each group into its own 32-bit lane; the
; second half is loaded twelve bytes along because VPSHUFB cannot cross a 128-bit lane.  Leads 0xE0
; and 0xED have narrower second bytes and are declined here, gen16 below takes them.
; -------------------------------------------------------------------------------------------------
ALIGN 16
three8:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 28
        jb      gen64
        lea     rax, [r15 + 8]
        cmp     rax, rdi
        ja      gen64
        vmovdqu xmm0, xmmword ptr [rsi + r14]
        vinserti128 ymm0, ymm0, xmmword ptr [rsi + r14 + 12], 1
        vpshufb ymm0, ymm0, ymmword ptr [CQ3SHUF]
        vpand   ymm1, ymm0, ymmword ptr [CQ3MSKd]
        vpcmpeqd ymm1, ymm1, ymmword ptr [CQ3WNTd]
        vpmovmskb eax, ymm1
        cmp     eax, -1
        jne     gen64
        vpand   ymm1, ymm0, ymmword ptr [CQ0Fd]
        vpxor   ymm2, ymm2, ymm2
        vpcmpeqd ymm2, ymm1, ymm2
        vpcmpeqd ymm1, ymm1, ymmword ptr [CQ0Dd]
        vpor    ymm1, ymm1, ymm2
        vpmovmskb eax, ymm1
        test    eax, eax
        jnz     gen64
        vpand   ymm1, ymm0, ymmword ptr [CQ0Fd]
        vpslld  ymm1, ymm1, 12
        vpsrld  ymm2, ymm0, 8
        vpand   ymm2, ymm2, ymmword ptr [CQ3Fd]
        vpslld  ymm2, ymm2, 6
        vpor    ymm1, ymm1, ymm2
        vpsrld  ymm2, ymm0, 16
        vpand   ymm2, ymm2, ymmword ptr [CQ3Fd]
        vpor    ymm1, ymm1, ymm2
        vpackusdw ymm1, ymm1, ymm1
        vpermq  ymm1, ymm1, 8
        vmovdqu xmmword ptr [rbx + r12*2], xmm1
        add     r14, 24
        add     r15, 8
        add     r12, 8
        and     r12, qword ptr [rsp + F_STOREMASK]
        jmp     mainloop

; -------------------------------------------------------------------------------------------------
; The four-byte block (change 034): sixteen bytes, four sequences, four surrogate pairs.
; A four-byte sequence IS a 32-bit lane.  Lead 0xF0 is taken (it carries every emoji there is) with
; one extra condition on its second byte; 0xF4 is declined and falls to gen16.
; -------------------------------------------------------------------------------------------------
ALIGN 16
four8:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 16
        jb      gen64
        lea     rax, [r15 + 8]
        cmp     rax, rdi
        ja      gen64
        vmovdqu xmm0, xmmword ptr [rsi + r14]
        vpand   xmm1, xmm0, xmmword ptr [CQ4MSKd]
        vpcmpeqd xmm1, xmm1, xmmword ptr [CQ4WNTd]
        vpmovmskb eax, xmm1
        cmp     eax, 0FFFFh
        jne     gen64
        vpand   xmm1, xmm0, xmmword ptr [CQ03d]
        vpxor   xmm2, xmm2, xmm2
        vpcmpeqd xmm1, xmm1, xmm2
        vpand   xmm2, xmm0, xmmword ptr [CQ30d]
        vpxor   xmm3, xmm3, xmm3
        vpcmpeqd xmm2, xmm2, xmm3
        vpand   xmm1, xmm1, xmm2
        vpmovmskb eax, xmm1
        test    eax, eax
        jnz     gen64                           ; an overlong four-byte sequence
        vpand   xmm1, xmm0, xmmword ptr [CQ07d]
        vpslld  xmm1, xmm1, 18
        vpsrld  xmm2, xmm0, 8
        vpand   xmm2, xmm2, xmmword ptr [CQ3Fd]
        vpslld  xmm2, xmm2, 12
        vpor    xmm1, xmm1, xmm2
        vpsrld  xmm2, xmm0, 16
        vpand   xmm2, xmm2, xmmword ptr [CQ3Fd]
        vpslld  xmm2, xmm2, 6
        vpor    xmm1, xmm1, xmm2
        vpsrld  xmm2, xmm0, 24
        vpand   xmm2, xmm2, xmmword ptr [CQ3Fd]
        vpor    xmm1, xmm1, xmm2
        vpsubd  xmm1, xmm1, xmmword ptr [CQ10000d]
        vpsrld  xmm2, xmm1, 10
        vpaddd  xmm2, xmm2, xmmword ptr [CQD800d]
        vpand   xmm1, xmm1, xmmword ptr [CQ3FFd]
        vpaddd  xmm1, xmm1, xmmword ptr [CQDC00d]
        vpslld  xmm1, xmm1, 16
        vpor    xmm1, xmm1, xmm2
        vmovdqu xmmword ptr [rbx + r12*2], xmm1
        add     r14, 16
        add     r15, 8
        add     r12, 8
        and     r12, qword ptr [rsp + F_STOREMASK]
        jmp     mainloop

; =================================================================================================
; gen16, sixteen bytes of arbitrary UTF-8.  This is the block change 034 Does not have.
;
; Every block above needs its bytes to be the same kind of sequence.  Real text is not: English
; prose with an accent and a euro sign is one-, two- and three-byte characters interleaved, and
; discovery/utf8_width_mixtures.c measured exactly what that costs a decoder built only from runs
; 0.34x to 0.63x against ntdll on eight such classes.
;
; This block makes no assumption about the widths.  It works one INPUT POSITION per 16-bit lane,
; with two more copies of the input shifted down by one and two bytes so that every lane can see
; the three bytes that follow it, and it proves the whole 16-byte window in bit masks:
;
;   Classes, one vpcmpeqb each, collected by vpmovmskb into 16-bit masks:
;       mc = continuation (b & 0xC0) == 0x80      m3 = (b & 0xF0) == 0xE0
;       m2 = (b & 0xE0) == 0xC0                   m4 = (b & 0xF8) == 0xF0
;
;   THE CUT.  A lead near the end of the window owns bytes past it, so the window has to stop at
;   the last COMPLETE sequence.  A 2-byte lead spills only from position 15, a 3-byte lead from 14,
;   a 4-byte lead from 13, so the earliest spilling lead is one TZCNT, and the cut is never below
;   13, which is what guarantees forward progress.
;
;   The structure is one comparison.  The continuations the leads demand are
;       ((m2|m3|m4) << 1) | ((m3|m4) << 2) | (m4 << 3)
;   and that must equal the continuations PRESENT, inside the cut.  One CMP says every lead has its
;   continuations, no continuation is stranded, and position 0 is a character start.
;
;   THE VALUES that are not decided by that structure are decided in the byte domain, on the lead
;   and the byte after it, before any decoding happens: 0xC0/0xC1 (overlong two), 0xE0 with a second
;   byte below 0xA0 (overlong three), 0xED above 0x9F (an encoded surrogate), 0xF0 below 0x90
;   (overlong four), 0xF4 above 0x8F (beyond U+10FFFF), and anything above 0xF4.  Unsigned byte
;   comparisons do not exist, so the operands are biased by 0x80 once and compared signed.
;
;   a four-byte sequence produces two units, which would break a one-unit-per-position compaction.
;   It does not, because of an identity: the HIGH surrogate needs only bytes 0..2 of the sequence
;   (0xD7C0 + (cp >> 10), and cp >> 10 drops byte 3 entirely) and the LOW surrogate needs only
;   bytes 2..3 (0xDC00 | (cp & 0x3FF)).  So the high surrogate is computed in the LEAD's lane and
;   the low surrogate in the lane AFTER it, where bytes 2 and 3 are that lane's own +1 and +2 --
;   and the pair falls out of the same one-unit-per-position compaction as everything else.
;   The set of kept positions is then (~mc | (m4 << 1)).
;
; Eighteen bytes are read to consume at most sixteen, and the room test is for sixteen units,
; because sixteen ASCII bytes are sixteen units.
; =================================================================================================
ALIGN 16
; -------------------------------------------------------------------------------------------
; gen64, sixty-four bytes of arbitrary UTF-8, lifted from change 034's tgl variant.  It is
; reached exactly where `gen16` was, so every block above declines into it and it declines into
; `gen16_small` below, which is the parent's gen16 unchanged.
; -------------------------------------------------------------------------------------------
; change 034's `gen64s` entry is NOT carried over: it exists there because that dispatch sends a
; stray continuation and 0xF5..0xFF to the block, and THIS dispatch sends them to the scalar walk.
; A label nothing can reach, carrying a room check nothing evaluates, is worse than no label.
ALIGN 16
gen64:
        ; Short input is gen16_small's, not this block's, and leaving that out cost the 8-byte
        ; classes 0.65x-0.82x on the first run of this variant. The masked load makes gen64 CORRECT
        ; at any length (it cannot read past the buffer) so the first draft let it take
        ; everything, and correctness said nothing because nothing was wrong. It is simply the
        ; heavier instrument: a 64-byte gather-and-compact to decode eight bytes, where the parent
        ; handed those to a 16-byte block that declines to the scalar walk. Below 18 bytes this
        ; hands over to the block that owns the decision, which then applies its own rule.
        mov       rax, r13
        sub       rax, r14
        cmp       rax, 18
        jb        gen16_small
        mov       eax, edi
        sub       eax, r15d
        cmp       eax, 2
        jb        gen16_small
gen64_load:
        ; -----------------------------------------------------------------------------------------
        ; The load is masked, which is both the page guard and the end-of-buffer rule.
        ;
        ; An AVX-512 masked load does not fault on a masked-off element, so reading 64 bytes with
        ; only the bytes that exist enabled cannot touch the next page. There is therefore no
        ; "64 bytes must remain" guard and no tail handed to a slower block. The first draft did
        ; have that guard, and the 64-byte row of ASCII-with-a-bad-byte came in at 0.40x, because a
        ; 64-byte buffer never has 64 bytes LEFT once the ASCII block has eaten the front of it.
        ;
        ; The bytes past the end read as ZERO, and that is not a hazard; it is the truncation rule
        ; for free. 0x00 is never a continuation, so it ends a sequence that would have run past the
        ; buffer at exactly the byte the shipped decoder ends it at: `E2 82` as the last two bytes
        ; is one U+FFFD and two bytes consumed, on both sides. All that is left to do is to not
        ; COUNT the padding, which is what the two masks below are for.
        ; -----------------------------------------------------------------------------------------
        mov       r11d, r13d
        sub       r11d, r14d                        ; the bytes that really remain
        mov       rdx, -1
        cmp       r11d, 64
        jb        gen64_part
        ; a full block is sixty-four bytes, however many remain. r11 is both the width of the
        ; masked store below and the amount the two cursors advance by at the end, so leaving it
        ; at the whole remaining length made this block convert 64 bytes and then claim the
        ; Entire rest of the buffer as finished: the first 64 units were written and correct, the
        ; source cursor jumped to the end, and the returned count came out right by accident
        ; because it is derived from that same cursor. probes/onebad.c pins it, one byte the
        ; blocks cannot decode anywhere in the first 64, and every unit from 64 on is unwritten.
        mov       r11d, 64
        jmp       gen64_wide
gen64_part:
        bzhi      rdx, rdx, r11                     ; a short block: only these bytes exist
gen64_wide:
        kmovq     k1, rdx
        kmovq     k0, rdx                           ; kept here: every GPR below is spoken for
        vmovdqu8  zmm0{k1}{z}, zmmword ptr [rsi + r14]

        ; -----------------------------------------------------------------------------------------
        ; ASCII with rubbish in it, the shape that log files, network buffers and user input
        ; actually have, and the one the general path is worst at.
        ;
        ; If NO byte in the block is 0xC2..0xF4 then no byte can lead a multi-byte sequence, so every
        ; one of the 64 is its own subpart: a byte below 0x80 is itself, and every other byte, a
        ; stray continuation, 0xC0/0xC1, 0xF5..0xFF, is one U+FFFD and one byte, which is exactly
        ; what the shipped decoder does with it. Sixty-four bytes become sixty-four units with two
        ; widenings and a masked load, and none of the position arithmetic below is needed.
        ;
        ; Why it earns its place. Without it the general path swallowed the whole buffer on this
        ; input (discovery/utf8_width_mixtures.c's "one bad byte every 32/64" rows) at about ten
        ; times the cost per byte of the ASCII block, because one stray byte in sixty-four is enough
        ; to keep the ASCII block out. The two comparisons that decline it cost four instructions on
        ; every other kind of block.
        ; -----------------------------------------------------------------------------------------
        vpcmpub   k1, zmm0, zmmword ptr [GC2], 5
        vpcmpub   k2, zmm0, zmmword ptr [GF5], 1
        kandq     k1, k1, k2
        kortestq  k1, k1
        jnz       gen64_full                        ; something in here really does lead a sequence
        mov       eax, edi
        sub       eax, r15d
        cmp       eax, r11d
        jb        gen64_full                        ; this many bytes is exactly this many units
        vpmovb2m  k1, zmm0                          ; top bit set = not ASCII = one U+FFFD.
        kmovq     rax, k1                           ; the padding reads 0x00, so it is never set
        vpmovzxbw zmm1, ymm0
        vextracti64x4 ymm2, zmm0, 1
        vpmovzxbw zmm2, ymm2
        kmovd     k2, eax
        mov       rcx, rax
        shr       rcx, 32
        kmovd     k3, ecx
        vmovdqu16 zmm1{k2}, zmmword ptr [GFFFDW]
        vmovdqu16 zmm2{k3}, zmmword ptr [GFFFDW]
        kmovd     k4, edx                           ; rdx is still `rm`: the store is masked to the
        mov       rcx, rdx                          ; bytes that exist, so a short block cannot put
        shr       rcx, 32                           ; its own padding into the caller's buffer
        kmovd     k5, ecx
        vmovdqu16 zmmword ptr [rbx + r12*2]{k4}, zmm1
        vmovdqu16 zmmword ptr [rbx + r12*2 + 64]{k5}, zmm2
        test      rax, rax
        setnz     al
        movzx     eax, al
        or        dword ptr [rsp + F_NOTMAP], eax
        add       r15, r11
        add       r12, r11                          ; the STORE index tracks it ...
        and       r12, qword ptr [rsp + F_STOREMASK] ; ... and is pinned when measuring
        add       r14, r11
        jmp       mainloop

gen64_full:
        ; ---- the verdict byte: every malformed class, for all 64 bytes, in three lookups ----
        vpandq    zmm1, zmm0, zmmword ptr [GNIB]
        vpsrlw    zmm2, zmm0, 4
        vpandq    zmm2, zmm2, zmmword ptr [GNIB]
        vmovdqu8  zmm17, zmmword ptr [GID1]
        vpermb    zmm3, zmm1, zmmword ptr [GTLO]
        vpermb    zmm5, zmm2, zmmword ptr [GTHI]
        vpermb    zmm19, zmm17, zmm2                ; the high nibble of the byte AFTER this one
        vpermb    zmm19, zmm19, zmmword ptr [GTB2]
        vpternlogd zmm3, zmm5, zmm19, 80h
        vpermb    zmm1, zmm2, zmmword ptr [GEXP]    ; the length each byte asks for

        ; ---- the six masks the maximal-subpart rule is built from ----
        vpcmpb    k1, zmm0, zmmword ptr [GC0], 1    ; signed < -64: a continuation byte
        vpcmpub   k2, zmm0, zmmword ptr [GC0], 5    ; >= 0xC0: takes a second byte
        vpcmpub   k3, zmm0, zmmword ptr [GE0], 5    ; >= 0xE0: a third
        vpcmpub   k4, zmm0, zmmword ptr [GF0], 5    ; >= 0xF0: a fourth
        vptestmb  k5, zmm3, zmmword ptr [G30]       ; 0xC0, 0xC1, 0xF5..0xFF -- never a lead
        vptestmb  k6, zmm3, zmmword ptr [G0F]       ; E0/ED/F0/F4 with a second byte out of range
        kmovq     rax, k1
        kmovq     r9,  k2
        kmovq     r10, k3
        kmovq     r11, k4
        kmovq     rcx, k5
        kmovq     rdx, k6

        ; ---- Which bytes are swallowed by the sequence in front of them ----
        ; A byte belongs to the character before it only if it is a continuation AND some lead
        ; close enough behind it really reaches it. In mask arithmetic, for byte i:
        ;   the lead at i-1 takes a second byte, or
        ;   the lead at i-2 takes a third AND i-1 was a continuation AND its second byte was in
        ;     range (an out-of-range second byte ENDS the subpart at two bytes, ntdll's twist), or
        ;   the lead at i-3 takes a fourth AND i-1 and i-2 were continuations AND likewise.
        ; An invalid lead reaches nothing, which is what makes 0xC0/0xF5 one byte and one U+FFFD.
        ; Everything NOT swallowed starts a maximal subpart (valid or not) and that is the
        ; boundary set the shipped decoder uses. It is the whole malformed rule, in twenty
        ; instructions, with no branch and no character-at-a-time walk.
        andn      r9, rcx, r9                       ; a lead that can take a second byte
        andn      r10, rcx, r10                     ; ... a third
        andn      r11, rcx, r11                     ; ... a fourth
        mov       rcx, rax
        shl       rcx, 1                            ; a continuation one byte back
        shl       r9, 1
        shl       r10, 2
        shl       r11, 3
        and       r10, rcx
        and       r11, rcx
        shl       rcx, 1                            ; ... and two bytes back
        and       r11, rcx
        mov       rcx, rdx
        shl       rcx, 2
        andn      r10, rcx, r10                     ; the out-of-range second byte stops it here
        shl       rdx, 3
        andn      r11, rdx, r11
        or        r9, r10
        or        r9, r11
        and       rax, r9                           ; swallowed
        not       rax                               ; S: every byte that STARTS a subpart
        ; ... and now drop the padding. A subpart ENDS at e when the next byte starts one, so the
        ; END mask is S>>1 confined to the bytes that exist, which, in a SHORT block, picks up the
        ; last real subpart precisely because the first pad byte is a zero and therefore a start.
        ; The START mask keeps one extra bit, at that pad byte, so that "the m'th start" is still
        ; defined when m is every subpart there is. That lane is never decoded.
        kmovq     rdx, k0                           ; the bytes that exist
        mov       r9, rdx
        shl       r9, 1
        or        r9, 1
        and       r9, rax                           ; the starts, plus the first pad byte
        kmovq     k2, r9
        mov       r11, rax
        shr       r11, 1
        and       r11, rdx
        kmovq     k3, r11
        popcnt    r8, r11                           ; how many subparts are complete
        ; The whole block, not a fixed slice of it. Consecutive blocks are serialised through the
        ; source cursor and the output cursor, so the cost of one is its DEPENDENCE CHAIN, about
        ; the same whether it decodes sixteen subparts or sixty. Capping it at 32 halved the rate on
        ; ASCII-dominated input for nothing. The only real ceiling is the destination: m subparts
        ; can be 2*m units, so m is clamped by half the room that is left, and never by a constant.
        mov       eax, edi                          ; rax is free now; r9 carries the start mask
        sub       eax, r15d
        shr       eax, 1
        cmp       r8d, eax
        cmova     r8d, eax                          ; m subparts can be 2*m units
        test      r8d, r8d
        jz        gen_none                          ; no room for even one -- let the scalar rule
        mov       rcx, r8                           ; produce the partial write and the status
        mov       r11, 1
        shl       r11, cl
        pdep      r11, r11, r9                      ; isolate the m'th subpart start ...
        tzcnt     r10, r11                          ; ... which is what this block consumes

        ; ---- lengths, and which subparts are well formed ----
        vmovdqu8  zmm16, zmmword ptr [GID]
        vpcompressb zmm4{k3}{z}, zmm17              ; p(j+1)
        vpcompressb zmm5{k2}{z}, zmm16              ; p(j)
        vpcompressb zmm19{k2}{z}, zmm1              ; the length its first byte asks for
        vpsubb    zmm18, zmm4, zmm5                 ; the length it has
        vpcmpb    k7, zmm18, zmm19, 4               ; they disagree: substitute one U+FFFD
        ; One test decides every malformed class, because the boundary rule above already placed
        ; the end of the subpart: a truncated sequence is short, an invalid lead is one byte
        ; against an expected two or four, a stray continuation is one byte against an expected
        ; ZERO, and an out-of-range second byte is two bytes against an expected three or four.

        mov       r11, -1
        bzhi      r11, r11, r8
        kmovq     k6, r11
        kandq     k6, k6, k7
        kortestq  k6, k6
        setnz     al
        movzx     eax, al
        or        dword ptr [rsp + F_NOTMAP], eax              ; STATUS_SOME_NOT_MAPPED

        ; ---- decode, sixteen subparts at a time, until the block is spent ----
        vmovdqu8  zmm27, zmmword ptr [GREPA]
gen_pass:
        mov       r9d, r8d
        cmp       r9d, 16
        jbe       gen_last
        mov       r9d, 16
gen_last:
        GENHALF   zmm27
        sub       r8d, r9d
        jz        gen_done
        kshiftrq  k7, k7, 16                        ; the next sixteen verdicts ...
        vpaddb    zmm27, zmm27, zmmword ptr [G16]   ; ... and the next sixteen subparts
        jmp       gen_pass
gen_done:
        add       r14, r10
        jmp       mainloop
gen_none:
ALIGN 16
gen16_small:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 18
        jb      scalar_win
        lea     rax, [r15 + 16]
        cmp     rax, rdi
        ja      scalar_win

        vmovdqu xmm0, xmmword ptr [rsi + r14]           ; b0
        vmovdqu xmm1, xmmword ptr [rsi + r14 + 1]       ; b1

        ; ---- classes ----
        vpand   xmm2, xmm0, xmmword ptr [BC0]
        vpcmpeqb xmm2, xmm2, xmmword ptr [B80]
        vpmovmskb ecx, xmm2                             ; mc: continuations
        vpand   xmm2, xmm0, xmmword ptr [BE0]
        vpcmpeqb xmm2, xmm2, xmmword ptr [BC0]
        vpmovmskb edx, xmm2                             ; m2: 0xC0..0xDF
        vpand   xmm2, xmm0, xmmword ptr [BF0]
        vpcmpeqb xmm2, xmm2, xmmword ptr [BE0]
        vpmovmskb r8d, xmm2                             ; m3: 0xE0..0xEF
        vpand   xmm2, xmm0, xmmword ptr [BF8]
        vpcmpeqb xmm2, xmm2, xmmword ptr [BF0]
        vpmovmskb r9d, xmm2                             ; m4: 0xF0..0xF7

        ; ---- the bytes that are wrong whatever the structure says ----
        vpxor   xmm2, xmm0, xmmword ptr [B80]           ; b0 biased for signed comparison
        vpcmpgtb xmm3, xmm2, xmmword ptr [B74]          ; 0xF5..0xFF
        vpand   xmm2, xmm0, xmmword ptr [BFE]
        vpcmpeqb xmm2, xmm2, xmmword ptr [BC0]          ; 0xC0 / 0xC1
        vpor    xmm3, xmm3, xmm2
        vpxor   xmm4, xmm1, xmmword ptr [B80]           ; b1 biased
        vpcmpgtb xmm5, xmm4, xmmword ptr [B1F]          ; b1 > 0x9F
        vpcmpeqb xmm2, xmm0, xmmword ptr [BE0]
        vpandn  xmm2, xmm5, xmm2                        ; 0xE0 with b1 < 0xA0 : overlong
        vpor    xmm3, xmm3, xmm2
        vpcmpeqb xmm2, xmm0, xmmword ptr [BED]
        vpand   xmm2, xmm2, xmm5                        ; 0xED with b1 > 0x9F : a surrogate
        vpor    xmm3, xmm3, xmm2
        vpcmpgtb xmm5, xmm4, xmmword ptr [B0F]          ; b1 > 0x8F
        vpcmpeqb xmm2, xmm0, xmmword ptr [BF0]
        vpandn  xmm2, xmm5, xmm2                        ; 0xF0 with b1 < 0x90 : overlong
        vpor    xmm3, xmm3, xmm2
        vpcmpeqb xmm2, xmm0, xmmword ptr [BF4]
        vpand   xmm2, xmm2, xmm5                        ; 0xF4 with b1 > 0x8F : > U+10FFFF
        vpor    xmm3, xmm3, xmm2
        vpmovmskb r10d, xmm3                            ; the bad-byte mask

        ; ---- the cut: the last complete sequence inside the window ----
        ; The window mask is built with blsmsk rather than tzcnt + cmov, because everything below
        ; waits on it: `spill OR 0x10000` is never zero, BLSMSK gives the bits up to and including
        ; the lowest set one, and one shift turns that into (1 << cut) - 1.  Five dependent
        ; instructions instead of eight, on the chain that decides the whole block.
        mov     eax, edx
        and     eax, 8000h                              ; a two-byte lead at 15
        mov     r11d, r8d
        and     r11d, 0C000h                            ; a three-byte lead at 14 or 15
        or      eax, r11d
        mov     r11d, r9d
        and     r11d, 0E000h                            ; a four-byte lead at 13, 14 or 15
        or      eax, r11d
        or      eax, 10000h                             ; the window ends at 16 if nothing spills
        blsmsk  r11d, eax
        shr     r11d, 1                                 ; mask = (1 << cut) - 1, cut always >= 13

        and     r10d, r11d
        jnz     scalar_win                              ; a byte no structure can rescue

        ; ---- the structure, in one comparison ----
        ; The leads are masked by the cut; the demand is not, and that asymmetry is the whole test.
        ; Masking the demand as well passed this input:
        ;       C2 80 C3 81 C4 82 C5 83 C6 84 C7 85 C8 F4 C9 87
        ; The 0xF4 at position 13 is a four-byte lead, so it spills and the cut is 13, and the
        ; 0xC8 at position 12 is then a lead INSIDE the window whose continuation is OUTSIDE it.
        ; With the demand masked too, its demand for position 13 disappeared and the block happily
        ; decoded `C8 F4` as U+0234 where the shipped export substitutes two U+FFFD.  Leaving the
        ; demand unmasked makes an orphaned lead show up as a bit the present continuations cannot
        ; match, so the block declines and the scalar walk gets it right.
        mov     eax, edx
        or      eax, r8d
        or      eax, r9d
        mov     r10d, r8d
        or      r10d, r9d
        and     r9d, r11d                               ; four-byte leads inside the window
        and     r10d, r11d                              ; ... three- and four-byte
        and     eax, r11d                               ; ... all of them
        add     eax, eax                                ; every lead demands one continuation
        shl     r10d, 2                                 ; three- and four-byte leads demand a second
        or      eax, r10d
        mov     r10d, r9d
        shl     r10d, 3                                 ; four-byte leads demand a third
        or      eax, r10d                               ; the demand, NOT masked by the cut
        mov     r10d, ecx
        and     r10d, r11d
        cmp     eax, r10d
        jne     scalar_win

        ; ---- which positions carry a unit, and how far the window reached ----
        popcnt  r8d, r11d                               ; cut = the number of bytes consumed
        mov     eax, r9d
        add     eax, eax                                ; the low surrogate's lane
        not     ecx
        or      ecx, eax
        and     ecx, r11d                               ; kept

; The measuring mode stops here.  cchWideChar == 0 asks only how many units there are, and the
; structure test has already proved that `kept` has one bit per unit, so everything below, the
; decode and the compaction and the stores, is work whose result is thrown away.  One
; well-predicted branch buys all of it back, and it is still the same decoder: the count comes from
; the same mask the conversion compacts by, so the two cannot drift apart.
        cmp     qword ptr [rsp + F_STOREMASK], 0
        je      gen16_count

        ; ---- the values ----
        vpmovzxbw ymm0, xmm0                            ; w0: the byte at this position
        vpmovzxbw ymm1, xmm1                            ; w1: the byte after it
        vpmovzxbw ymm2, xmmword ptr [rsi + r14 + 2]     ; w2: two after it
        vpand   ymm3, ymm0, ymmword ptr [W001F]
        vpsllw  ymm3, ymm3, 6
        vpand   ymm4, ymm1, ymmword ptr [W003F]
        vpor    ymm3, ymm3, ymm4                        ; the two-byte value
        vpand   ymm4, ymm0, ymmword ptr [W00E0]
        vpcmpeqw ymm4, ymm4, ymmword ptr [W00C0]
        vpblendvb ymm3, ymm0, ymm3, ymm4                ; ASCII keeps itself; a lead takes it
        vpand   ymm4, ymm0, ymmword ptr [W000F]
        vpsllw  ymm4, ymm4, 12
        vpand   ymm5, ymm1, ymmword ptr [W003F]
        vpsllw  ymm5, ymm5, 6
        vpor    ymm4, ymm4, ymm5
        vpand   ymm5, ymm2, ymmword ptr [W003F]
        vpor    ymm4, ymm4, ymm5                        ; the three-byte value
        vpand   ymm5, ymm0, ymmword ptr [W00F0]
        vpcmpeqw ymm5, ymm5, ymmword ptr [W00E0]
        vpblendvb ymm3, ymm3, ymm4, ymm5
        test    r9d, r9d
        jz      gen16_store

        ; ---- the surrogate pair, split across two lanes (see the header above) ----
        vpand   ymm4, ymm0, ymmword ptr [W0007]
        vpsllw  ymm4, ymm4, 8
        vpand   ymm5, ymm1, ymmword ptr [W003F]
        vpsllw  ymm5, ymm5, 2
        vpor    ymm4, ymm4, ymm5
        vpsrlw  ymm5, ymm2, 4
        vpand   ymm5, ymm5, ymmword ptr [W0003]
        vpor    ymm4, ymm4, ymm5
        vpaddw  ymm4, ymm4, ymmword ptr [WD7C0]         ; 0xD800 + ((cp - 0x10000) >> 10)
        vpand   ymm5, ymm0, ymmword ptr [W00F8]
        vpcmpeqw ymm5, ymm5, ymmword ptr [W00F0]        ; the lead's lane
        vpblendvb ymm3, ymm3, ymm4, ymm5
        vperm2i128 ymm4, ymm5, ymm5, 08h
        vpalignr ymm5, ymm5, ymm4, 14                   ; ... and the lane after it
        vpand   ymm4, ymm1, ymmword ptr [W000F]
        vpsllw  ymm4, ymm4, 6
        vpand   ymm0, ymm2, ymmword ptr [W003F]
        vpor    ymm4, ymm4, ymm0
        vpor    ymm4, ymm4, ymmword ptr [WDC00]         ; 0xDC00 | (cp & 0x3FF)
        vpblendvb ymm3, ymm3, ymm4, ymm5

; -------------------------------------------------------------------------------------------------
; The store, and why it is not two plain 16-BYTE stores.
;
; The compaction produces sixteen words of which only `kept` carry a unit, so a plain store writes
; up to eight words of ZEROS past the end of the output, inside the caller's capacity, but where
; the shipped export leaves the caller's own bytes alone.  correctness.c compares the whole
; capacity, and it caught exactly that: `ours: ... 6141 0000 0000` against `live: ... 6141 2323 2323`.
; Change 016's correctness.c had already written the rule down: nothing may be written past the
; end of the string, not merely past the capacity.
;
; The fix costs four instructions and no branch.  The bytes the HIGH half's store would spoil are
; read ONCE, BEFORE anything is written, and blended back in afterwards under a mask selected by
; how many units that half produced.  The LOW half needs no blend of its own: its overshoot is
; [o+cnt_low, o+8) and the high store covers [o+cnt_low, o+cnt_low+8), which contains it, but
; only because the preserved bytes were read before the low store ran.
; -------------------------------------------------------------------------------------------------
gen16_store:
        lea     r10, [MIXTAB]
        mov     eax, ecx
        and     eax, 0FFh
        popcnt  r9d, eax                        ; units from positions 0..7
        shl     eax, 4
        shr     ecx, 8
        popcnt  r11d, ecx                       ; units from positions 8..15
        shl     ecx, 4
        lea     rdx, [r12 + r9]                 ; where the second half lands
        vmovdqu xmm2, xmmword ptr [rbx + rdx*2] ; ... read its slack BEFORE anything is written
        vpshufb xmm4, xmm3, xmmword ptr [r10 + rax]
        vmovdqu xmmword ptr [rbx + r12*2], xmm4
        vextracti128 xmm3, ymm3, 1
        vpshufb xmm4, xmm3, xmmword ptr [r10 + rcx]
        mov     eax, r11d
        shl     eax, 4
        vmovdqu xmm5, xmmword ptr [r10 + rax + MTOFF]
        vpblendvb xmm4, xmm2, xmm4, xmm5        ; the caller's bytes survive past the output
        vmovdqu xmmword ptr [rbx + rdx*2], xmm4
        add     r15, r9
        add     r15, r11
        lea     r12, [rdx + r11]
        and     r12, qword ptr [rsp + F_STOREMASK]
        add     r14, r8
        jmp     mainloop

; the measuring mode lands here instead: the count is the population of the same mask
gen16_count:
        popcnt  eax, ecx
        add     r15, rax
        add     r14, r8
        jmp     mainloop

; =================================================================================================
; The scalar walk.  a scalar walk must not re-enter a vector loop (change 263's rule): once the
; blocks have declined, 32 source bytes are decoded one character at a time before they are tried
; again, so the probe is amortised over a cache line of input instead of over one character.
;
; The length and the byte-2 range come from ONE table load.  Change 034 derived both with two
; compare cascades, and discovery/utf8_width_mixtures.c named those cascades as where its time
; went once the re-entry was fixed.
;
; The maximal-subpart rule is delicate and the ORDER of the two byte-2 tests is the whole of it:
; the GENERIC continuation test (0x80..0xBF) decides whether byte 2 is consumed at all, and only
; then does the lead's own narrower range decide whether the sequence is valid.  A byte-2 that is
; a continuation but out of range is therefore consumed (one U+FFFD, advance TWO) while a
; byte-2 that is not a continuation is not: one U+FFFD, advance ONE.
; =================================================================================================
scalar_win:
        lea     eax, [r14 + 32]
        mov     dword ptr [rsp + F_WINDOW], eax

ALIGN 16
scalar:
        movzx   r8d, byte ptr [rsi + r14]
        cmp     r8d, 80h
        jb      sc_ascii
        mov     edx, dword ptr [rbp + r8*4]
        movzx   ecx, dl                         ; the expected length; 0 = never a lead
        test    ecx, ecx
        jz      sc_fffd1
        lea     rax, [r14 + 1]
        cmp     rax, r13
        jae     sc_fffd1                        ; truncated at the lead
        movzx   r9d, byte ptr [rsi + r14 + 1]   ; byte 2
        mov     eax, r9d
        sub     eax, 80h
        cmp     eax, 40h
        jae     sc_fffd1                        ; not a continuation at all: consume ONE
        movzx   r11d, byte ptr [rbp + r8*4 + 1] ; the lead's lower bound, biased by 0x80
        sub     eax, r11d
        movzx   r11d, byte ptr [rbp + r8*4 + 2] ; and that bound's span
        cmp     eax, r11d
        ja      sc_fffd2                        ; a continuation, out of range: consume TWO
        cmp     ecx, 2
        je      sc_two
        lea     rax, [r14 + 2]
        cmp     rax, r13
        jae     sc_fffd2
        movzx   r10d, byte ptr [rsi + r14 + 2]  ; byte 3
        mov     eax, r10d
        sub     eax, 80h
        cmp     eax, 40h
        jae     sc_fffd2
        cmp     ecx, 3
        je      sc_three
        lea     rax, [r14 + 3]
        cmp     rax, r13
        jae     sc_fffd3
        movzx   r11d, byte ptr [rsi + r14 + 3]  ; byte 4
        mov     eax, r11d
        sub     eax, 80h
        cmp     eax, 40h
        jae     sc_fffd3
        ; ---- a complete four-byte sequence: one surrogate pair, one room test EACH ----
        mov     eax, edx
        shr     eax, 24
        shl     eax, 18
        and     r9d, 3Fh
        shl     r9d, 12
        or      eax, r9d
        and     r10d, 3Fh
        shl     r10d, 6
        or      eax, r10d
        and     r11d, 3Fh
        or      eax, r11d
        sub     eax, 10000h
        cmp     r15, rdi
        jae     ov_stop
        mov     r10d, eax
        shr     r10d, 10
        add     r10d, 0D800h
        mov     word ptr [rbx + r12*2], r10w
        inc     r15
        inc     r12
        and     r12, qword ptr [rsp + F_STOREMASK]
        cmp     r15, rdi
        jae     ov_stop                         ; room for the high surrogate only: it stays written
        and     eax, 3FFh
        add     eax, 0DC00h
        mov     word ptr [rbx + r12*2], ax
        inc     r15
        inc     r12
        and     r12, qword ptr [rsp + F_STOREMASK]
        add     r14, 4
        jmp     scalar_next

sc_two:
        mov     eax, edx
        shr     eax, 24
        shl     eax, 6
        and     r9d, 3Fh
        or      eax, r9d
        cmp     r15, rdi
        jae     ov_stop
        mov     word ptr [rbx + r12*2], ax
        inc     r15
        inc     r12
        and     r12, qword ptr [rsp + F_STOREMASK]
        add     r14, 2
        jmp     scalar_next

sc_three:
        mov     eax, edx
        shr     eax, 24
        shl     eax, 12
        and     r9d, 3Fh
        shl     r9d, 6
        or      eax, r9d
        and     r10d, 3Fh
        or      eax, r10d
        cmp     r15, rdi
        jae     ov_stop
        mov     word ptr [rbx + r12*2], ax
        inc     r15
        inc     r12
        and     r12, qword ptr [rsp + F_STOREMASK]
        add     r14, 3
        jmp     scalar_next

sc_ascii:
        cmp     r15, rdi
        jae     ov_stop
        mov     word ptr [rbx + r12*2], r8w
        inc     r15
        inc     r12
        and     r12, qword ptr [rsp + F_STOREMASK]
        inc     r14
        jmp     scalar_next

sc_fffd1:
        mov     dword ptr [rsp + F_NOTMAP], 1
        cmp     r15, rdi
        jae     ov_stop
        mov     word ptr [rbx + r12*2], 0FFFDh
        inc     r15
        inc     r12
        and     r12, qword ptr [rsp + F_STOREMASK]
        inc     r14
        jmp     scalar_next

sc_fffd2:
        mov     dword ptr [rsp + F_NOTMAP], 1
        cmp     r15, rdi
        jae     ov_stop
        mov     word ptr [rbx + r12*2], 0FFFDh
        inc     r15
        inc     r12
        and     r12, qword ptr [rsp + F_STOREMASK]
        add     r14, 2
        jmp     scalar_next

sc_fffd3:
        mov     dword ptr [rsp + F_NOTMAP], 1
        cmp     r15, rdi
        jae     ov_stop
        mov     word ptr [rbx + r12*2], 0FFFDh
        inc     r15
        inc     r12
        and     r12, qword ptr [rsp + F_STOREMASK]
        add     r14, 3
        jmp     scalar_next

scalar_next:
        cmp     r14, r13
        jae     done
        mov     eax, dword ptr [rsp + F_WINDOW]
        cmp     r14d, eax
        jb      scalar                          ; still inside the window: stay scalar
        jmp     mainloop                        ; the window is spent: probe the blocks again

ov_stop:
        mov     dword ptr [rsp + F_OVER], 1

; ---- the three results -------------------------------------------------------------------------
done:
        cmp     dword ptr [rsp + F_OVER], 0
        jne     ret_buf
        cmp     dword ptr [rsp + F_NOTMAP], 0
        je      ret_ok
        test    dword ptr [rsp + F_FLAGS], 8    ; MB_ERR_INVALID_CHARS
        jz      ret_ok
        mov     eax, E_NOXLAT
        jmp     ret_err
ret_buf:
        mov     eax, E_BUF
ret_err:
        mov     dword ptr gs:[68h], eax
        xor     eax, eax
        jmp     epi
err_param_late:
        mov     dword ptr gs:[68h], E_PARAM
        xor     eax, eax
        jmp     epi
ret_ok:
        lea     rax, [r15*2]
        cmp     rax, 7FFFFFFFh
        ja      err_param_late                  ; the shipped code's own overflow rejection
        mov     eax, r15d
epi:
        add     rsp, F_SIZE
        vzeroupper
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rdi
        pop     rsi
        pop     rbx
        ret

; ---- the leaf exits: nothing has been pushed, so these are three instructions -------------------
err_param:
        mov     dword ptr gs:[68h], E_PARAM
        xor     eax, eax
        ret
err_flags:
        mov     dword ptr gs:[68h], E_FLAGS
        xor     eax, eax
        ret
delegate:
        mov     rax, qword ptr [wia_mbtwc_fallback]
        test    rax, rax
        jz      delegate_unset
        jmp     rax                             ; a true tail call: same frame, same return address
delegate_unset:
        mov     dword ptr gs:[68h], E_PARAM
        xor     eax, eax
        ret
wia_mbtwc ENDP

        END
