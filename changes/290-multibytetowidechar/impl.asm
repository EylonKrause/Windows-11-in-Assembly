; changes/290-multibytetowidechar/impl.asm
;
; int wia_mbtwc(UINT CodePage, DWORD dwFlags, const char* lpMultiByteStr, int cbMultiByte,
;               wchar_t* lpWideCharStr, int cchWideChar)
;   [Win64: rcx, edx, r8, r9d, [rsp+28h], [rsp+30h] -> eax; last error in gs:[68h]]
;
; Reimplements kernel32/kernelbase!MultiByteToWideChar for CodePage == CP_UTF8 (65001) only.
;
; ------------------------------------------------------------------------------------------------
; THE DISPATCH BOUNDARY, stated plainly.
;
;   CodePage == 65001  ->  every part of the contract is implemented HERE: parameter validation,
;                          flag validation, the NUL-terminated length (cbMultiByte < 0), the
;                          conversion, the MEASURING MODE (cchWideChar == 0), the per-unit
;                          overflow rule, MB_ERR_INVALID_CHARS, and the last-error values.
;   anything else      ->  TAIL-CALLED, unchanged, into the real export, before a single byte of
;                          this function's frame exists.  The caller cannot tell the difference:
;                          same registers, same stack, same return address.
;
; The fallback pointer is installed once by the caller with wia_mbtwc_set_fallback(); with no
; pointer installed a non-UTF-8 code page fails with ERROR_INVALID_PARAMETER rather than jumping
; to zero.
;
; ------------------------------------------------------------------------------------------------
; WHAT THE SHIPPED CODE ACTUALLY IS -- which is why this is worth doing.
;
; kernelbase!MultiByteToWideChar at RVA 0x1DAA0 opens with eight pushes, `sub rsp, 0F48h`, and a
; security cookie (`mov rax,[__security_cookie] / xor rax,rsp / mov [rbp+E30h],rax`, paid back
; through a call to __security_check_cookie on EVERY exit).  It then dispatches on the code page,
; validates, and for CP_UTF8 does this:
;
;       mov     [rsp+50h], r12d          ; produced = 0
;       cmovne  rcx, r8                  ; dst, or NULL when cchWideChar == 0
;       lea     edx, [r10+r10]           ; maxBytes = cchWideChar * 2
;       lea     r8,  [rsp+50h]           ; &produced
;       mov     r9,  rsi                 ; src
;       call    qword ptr [18029AD20h]   ; ntdll!RtlUTF8ToUnicodeN
;
; So the CP_UTF8 path IS ntdll!RtlUTF8ToUnicodeN plus a 3912-byte stack frame, a security cookie,
; an indirect import call, and -- for cbMultiByte < 0 -- a BYTE-AT-A-TIME strlen:
;
;       mov ecx,7FFFFFFFh / mov rax,rsi / cmp byte ptr [rax],r12b / je .. / inc rax / sub rcx,1 / jne
;
; and then maps the status: 0xC0000023 -> ERROR_INSUFFICIENT_BUFFER, any other negative ->
; ERROR_INVALID_PARAMETER, 0x107 with MB_ERR_INVALID_CHARS -> ERROR_NO_UNICODE_TRANSLATION.
; That last mapping is where the `test r15b,8` at 0x1E4B8 lives.
;
; This implementation replaces the whole of it: no frame beyond 96 bytes, no cookie, no call, an
; AVX2 strlen, and a decoder that is vectorised for MIXED-WIDTH input and not only for runs.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, ALL OF IT PROVED BY PROBING (probes/rule.c, 43,244,089 comparisons, 0 mismatches).
;
;   * flags: `and flags,~7` then `test flags,~8` -- i.e. 0x00..0x0F are ALL accepted and 0x10 and
;     above are ERROR_INVALID_FLAGS.  MB_PRECOMPOSED / MB_COMPOSITE / MB_USEGLYPHCHARS are ignored
;     for CP_UTF8, not rejected, which is not what MSDN says.
;   * parameters come FIRST: a bad flag together with cbMultiByte == 0 reports the parameter.
;   * the alias rule is EXACT POINTER EQUALITY (src == dst), and it is only checked when
;     cchWideChar != 0.  A destination that merely OVERLAPS the source is accepted.
;   * any negative cbMultiByte means NUL-terminated (not just -1), and the NUL is converted.
;   * the malformed rule is ntdll's maximal-subpart rule exactly, because it IS ntdll's code:
;     a byte-2 that is a generic continuation but outside the lead's special range is CONSUMED
;     (one U+FFFD, advance 2); a byte-2 that is not a continuation is not (one U+FFFD, advance 1).
;   * overflow is PER UNIT: a surrogate pair IS split when one slot remains, the first
;     cchWideChar units are written, and the call returns 0 / ERROR_INSUFFICIENT_BUFFER.
;   * ERROR_INSUFFICIENT_BUFFER WINS over ERROR_NO_UNICODE_TRANSLATION, wherever the bad byte is.
;   * on success the caller's last error is left untouched.
;
; ------------------------------------------------------------------------------------------------
; WHY THE DECODER IS NOT A COPY OF CHANGE 034.
;
; discovery/utf8_width_mixtures.c measured change 034 -- the same transformation one layer down --
; at 0.34x to 0.63x against ntdll on EIGHT MIXED-WIDTH input classes, while its published table
; said 3.96x.  Its five vector blocks each require their bytes to be the SAME KIND of sequence,
; which is the one thing real text never is, so mixed-width text fell to its scalar walk.
;
; This file keeps 034's homogeneous blocks, because a run of CJK or a run of Greek really is a run
; and they are the fastest thing for it, and adds the block that was missing: `gen16` decodes
; SIXTEEN BYTES OF ARBITRARY UTF-8 -- ASCII, two-, three- AND four-byte sequences interleaved in
; any order -- in one pass, with no assumption that the widths agree.  The scalar walk is then a
; malformed-input path and a tail, not the general case.
;
; ISA: AVX2 + BMI1 (TZCNT) + BMI2 (BZHI) + POPCNT.  No AVX-512, no GFNI: the implementation of
; record runs on all three validation benches.

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
F_SCRATCH       EQU     32                      ; 64 bytes: where the measuring mode's stores land
F_SIZE          EQU     104

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
; THE COMPACTION TABLE, generated by the assembler rather than pasted in.
;
; gen16 computes one UTF-16 unit per INPUT POSITION and then throws away the positions that carry
; no unit of their own -- the continuation bytes.  Which positions survive varies, so the only
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
; THE LEAD TABLE, also generated.  One dword per possible lead byte:
;
;       bits  0.. 7   the expected sequence length, or 0 for a byte that is never a lead
;       bits  8..15   the lead's own lower bound for byte 2, BIASED BY 0x80
;       bits 16..23   the span of that bound (hi - lo)
;       bits 24..31   the lead's payload bits, already masked
;
; This replaces two compare cascades in the scalar walk -- one to derive the length, one to derive
; the byte-2 range -- with a single load.  discovery/utf8_width_mixtures.c named those two cascades
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

        .code

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

; ---- THE MEASURING MODE IS THE SAME LOOP ------------------------------------------------------
; cchWideChar == 0 asks for the size and must write nothing.  Rather than carry a second decoder
; -- which is how the counting rule and the conversion rule drift apart -- the destination becomes
; a 64-byte scratch on this frame and the STORE INDEX is ANDed with a mask that is 0 in measuring
; mode and -1 otherwise.  Every store then lands on the scratch, the unit counter r15 is unaffected,
; and there is exactly one decoder to get right.  64 bytes is the widest any block writes.
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
; THE DISPATCH.  One byte decides, because that byte is the next character's LEAD and the blocks
; below are mutually exclusive.  Chaining them instead makes whichever block finally matches pay
; for every earlier one -- change 034 measured that at about a third of the mixed class's total.
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
        jb      gen16
        lea     rax, [r15 + 8]
        cmp     rax, rdi
        ja      gen16                           ; NOT the scalar path: every block tests its own
        vmovq   xmm0, qword ptr [rsi + r14]     ; room, so that no guard below becomes untestable
        vpmovmskb eax, xmm0
        and     eax, 0FFh
        jnz     gen16
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
; THE TWO-BYTE BLOCK (change 034): sixteen bytes, eight characters, no shuffle.
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
        jne     gen16                           ; not eight clean two-byte sequences
        vpand   xmm1, xmm0, xmmword ptr [CQ1Ew]
        vpxor   xmm2, xmm2, xmm2
        vpcmpeqw xmm1, xmm1, xmm2
        vpmovmskb eax, xmm1
        test    eax, eax
        jnz     gen16                           ; a 0xC0 or 0xC1 lead: overlong, substituted
        vpand   xmm1, xmm0, xmmword ptr [CQDECw]
        vpmaddubsw xmm1, xmm1, xmmword ptr [CQMULb]
        vmovdqu xmmword ptr [rbx + r12*2], xmm1
        add     r14, 16
        add     r15, 8
        add     r12, 8
        and     r12, qword ptr [rsp + F_STOREMASK]
        jmp     mainloop

; -------------------------------------------------------------------------------------------------
; THE THREE-BYTE BLOCK (change 034): twenty-four bytes consumed, TWENTY-EIGHT READ.
; Three divides neither 16 nor 32, so one VPSHUFB spreads each group into its own 32-bit lane; the
; second half is loaded twelve bytes along because VPSHUFB cannot cross a 128-bit lane.  Leads 0xE0
; and 0xED have narrower second bytes and are declined here -- gen16 below takes them.
; -------------------------------------------------------------------------------------------------
ALIGN 16
three8:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 28
        jb      gen16
        lea     rax, [r15 + 8]
        cmp     rax, rdi
        ja      gen16
        vmovdqu xmm0, xmmword ptr [rsi + r14]
        vinserti128 ymm0, ymm0, xmmword ptr [rsi + r14 + 12], 1
        vpshufb ymm0, ymm0, ymmword ptr [CQ3SHUF]
        vpand   ymm1, ymm0, ymmword ptr [CQ3MSKd]
        vpcmpeqd ymm1, ymm1, ymmword ptr [CQ3WNTd]
        vpmovmskb eax, ymm1
        cmp     eax, -1
        jne     gen16
        vpand   ymm1, ymm0, ymmword ptr [CQ0Fd]
        vpxor   ymm2, ymm2, ymm2
        vpcmpeqd ymm2, ymm1, ymm2
        vpcmpeqd ymm1, ymm1, ymmword ptr [CQ0Dd]
        vpor    ymm1, ymm1, ymm2
        vpmovmskb eax, ymm1
        test    eax, eax
        jnz     gen16
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
; THE FOUR-BYTE BLOCK (change 034): sixteen bytes, four sequences, four surrogate pairs.
; A four-byte sequence IS a 32-bit lane.  Lead 0xF0 is taken (it carries every emoji there is) with
; one extra condition on its second byte; 0xF4 is declined and falls to gen16.
; -------------------------------------------------------------------------------------------------
ALIGN 16
four8:
        mov     rax, r13
        sub     rax, r14
        cmp     rax, 16
        jb      gen16
        lea     rax, [r15 + 8]
        cmp     rax, rdi
        ja      gen16
        vmovdqu xmm0, xmmword ptr [rsi + r14]
        vpand   xmm1, xmm0, xmmword ptr [CQ4MSKd]
        vpcmpeqd xmm1, xmm1, xmmword ptr [CQ4WNTd]
        vpmovmskb eax, xmm1
        cmp     eax, 0FFFFh
        jne     gen16
        vpand   xmm1, xmm0, xmmword ptr [CQ03d]
        vpxor   xmm2, xmm2, xmm2
        vpcmpeqd xmm1, xmm1, xmm2
        vpand   xmm2, xmm0, xmmword ptr [CQ30d]
        vpxor   xmm3, xmm3, xmm3
        vpcmpeqd xmm2, xmm2, xmm3
        vpand   xmm1, xmm1, xmm2
        vpmovmskb eax, xmm1
        test    eax, eax
        jnz     gen16                           ; an overlong four-byte sequence
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
; gen16 -- SIXTEEN BYTES OF ARBITRARY UTF-8.  THIS IS THE BLOCK CHANGE 034 DOES NOT HAVE.
;
; Every block above needs its bytes to be the same kind of sequence.  Real text is not: English
; prose with an accent and a euro sign is one-, two- and three-byte characters interleaved, and
; discovery/utf8_width_mixtures.c measured exactly what that costs a decoder built only from runs
; -- 0.34x to 0.63x against ntdll on eight such classes.
;
; This block makes no assumption about the widths.  It works one INPUT POSITION per 16-bit lane,
; with two more copies of the input shifted down by one and two bytes so that every lane can see
; the three bytes that follow it, and it proves the whole 16-byte window in bit masks:
;
;   CLASSES, one VPCMPEQB each, collected by VPMOVMSKB into 16-bit masks:
;       mc = continuation (b & 0xC0) == 0x80      m3 = (b & 0xF0) == 0xE0
;       m2 = (b & 0xE0) == 0xC0                   m4 = (b & 0xF8) == 0xF0
;
;   THE CUT.  A lead near the end of the window owns bytes past it, so the window has to stop at
;   the last COMPLETE sequence.  A 2-byte lead spills only from position 15, a 3-byte lead from 14,
;   a 4-byte lead from 13 -- so the earliest spilling lead is one TZCNT, and the cut is never below
;   13, which is what guarantees forward progress.
;
;   THE STRUCTURE IS ONE COMPARISON.  The continuations the leads DEMAND are
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
;   A FOUR-BYTE SEQUENCE PRODUCES TWO UNITS, which would break a one-unit-per-position compaction.
;   It does not, because of an identity: the HIGH surrogate needs only bytes 0..2 of the sequence
;   (0xD7C0 + (cp >> 10), and cp >> 10 drops byte 3 entirely) and the LOW surrogate needs only
;   bytes 2..3 (0xDC00 | (cp & 0x3FF)).  So the high surrogate is computed in the LEAD's lane and
;   the low surrogate in the lane AFTER it -- where bytes 2 and 3 are that lane's own +1 and +2 --
;   and the pair falls out of the same one-unit-per-position compaction as everything else.
;   The set of kept positions is then (~mc | (m4 << 1)).
;
; Eighteen bytes are read to consume at most sixteen, and the room test is for sixteen units,
; because sixteen ASCII bytes are sixteen units.
; =================================================================================================
ALIGN 16
gen16:
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
        ; The window mask is built with BLSMSK rather than TZCNT + CMOV, because everything below
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
        ; THE LEADS ARE MASKED BY THE CUT; THE DEMAND IS NOT, and that asymmetry is the whole test.
        ; Masking the demand as well passed this input:
        ;       C2 80 C3 81 C4 82 C5 83 C6 84 C7 85 C8 F4 C9 87
        ; The 0xF4 at position 13 is a four-byte lead, so it spills and the cut is 13 -- and the
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

; THE MEASURING MODE STOPS HERE.  cchWideChar == 0 asks only how many units there are, and the
; structure test has already proved that `kept` has one bit per unit -- so everything below, the
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
; THE STORE, AND WHY IT IS NOT TWO PLAIN 16-BYTE STORES.
;
; The compaction produces sixteen words of which only `kept` carry a unit, so a plain store writes
; up to eight words of ZEROS past the end of the output -- inside the caller's capacity, but where
; the shipped export leaves the caller's own bytes alone.  correctness.c compares the WHOLE
; capacity, and it caught exactly that: `ours: ... 6141 0000 0000` against `live: ... 6141 2323 2323`.
; Change 016's correctness.c had already written the rule down: nothing may be written past the
; end of the string, not merely past the capacity.
;
; The fix costs four instructions and no branch.  The bytes the HIGH half's store would spoil are
; read ONCE, BEFORE anything is written, and blended back in afterwards under a mask selected by
; how many units that half produced.  The LOW half needs no blend of its own: its overshoot is
; [o+cnt_low, o+8) and the high store covers [o+cnt_low, o+cnt_low+8), which contains it -- but
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
; THE SCALAR WALK.  A SCALAR WALK MUST NOT RE-ENTER A VECTOR LOOP (change 263's rule): once the
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
; a continuation but out of range is therefore consumed -- one U+FFFD, advance TWO -- while a
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
