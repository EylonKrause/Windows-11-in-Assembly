; kernelbase.dll!WideCharToMultiByte  --  hand-written x86-64 reimplementation (2.71x vs shipped)
; source of truth: changes/289-widechartomultibyte/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/289-widechartomultibyte/impl.asm
;
; int wia_wc2mb(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar,
;               Lpstr lpMultiByteStr, int cbMultiByte, lpcch lpDefaultChar, lpbool lpUsedDefaultChar)
;
;   [Win64: ecx, edx, r8, r9d, [rsp+28h], [rsp+30h], [rsp+38h], [rsp+40h] -> eax]
;
; Reimplements kernelbase!WideCharToMultiByte for the UTF-8 code page. The shipped export is bound
; by 310 desktop modules and 126 startup modules on this machine -- the highest fan-in genuinely
; convertible function there is -- and discovery/desktop-startup-timings.md measures its CP_UTF8
; path at 0.235 ns/byte on ASCII and 1.071 ns/byte on Cyrillic, which is about 0.93 GB/s on the
; input UTF-8 exists for.
;
; =================================================================================================
; The dispatch boundary -- exactly which inputs run this code.
;
; Our code runs if and only if all five of these hold:
;
;       CodePage            == 65001 (CP_UTF8)
;       dwFlags             == 0
;       lpDefaultChar       == NULL
;       lpUsedDefaultChar   == NULL
;       and the call passes the shipped argument validation, namely
;             cchWideChar != 0, cbMultiByte >= 0, lpWideCharStr != NULL, and when
;             cbMultiByte != 0 also lpMultiByteStr != NULL and lpMultiByteStr != lpWideCharStr;
;       and the destination does not OVERLAP the source -- see the overlap note below, which is
;             there because the gate caught a real divergence and not because it looked risky.
;
; Every other input tail-calls the real export with `jmp qword ptr [__imp_WideCharToMultiByte]`,
; which the loader has bound directly to kernelbase!WideCharToMultiByte. That is one instruction,
; it preserves every argument register and the whole stack frame, and correctness.c proves the
; target is the same address GetProcAddress returns.
;
; That is not a hedge, it is the only correct answer for the rest of this function. The ACP and OEM
; code pages are OS data tables; the best-fit mapping behind WC_NO_BEST_FIT_CHARS / lpDefaultChar
; is more OS data; UTF-7 is a different encoder with its own state machine. Reimplementing those
; bit-exactly is not tractable and guessing at them is how a "faster" function corrupts text. For
; those inputs correctness.c compares identical code against itself and passes trivially -- which is
; the point, and the gate still drives all 8 code pages x 9 flag sets x every pointer shape through
; this function to prove the boundary is where this comment says it is.
;
; The argument-validation failures also tail-call, deliberately. The shipped code reports them with
; RtlSetLastWin32Error, which has two process-global debug hooks in it (a break-on-error value and a
; telemetry flag) that a bare `mov gs:[68h], ecx` would silently skip. Nothing has been written to
; the destination at that point, so handing the whole call back to the real export is exact by
; construction and costs one jump on a path that returns immediately. The one error our code must
; report itself is ERROR_INSUFFICIENT_BUFFER, which cannot be known until the conversion has run and
; has already written the partial output; re-running the real export there would be wrong, not just
; slow, because a caller is allowed to pass a destination that OVERLAPS the source (proved: only
; EXACT pointer equality is rejected) and the first pass may have changed what the second would read.
;
; =================================================================================================
; The contract, read out of the shipped disassembly (kernelbase 10.0.26100.9278, rva 0x00054A80) and
; then PROVED against the running export by probes/contract.c. See RESULTS.md for the transcript.
;
; The whole CP_UTF8 path is argument validation wrapped around ONE call to ntdll!RtlUnicodeToUTF8N
; -- confirmed by reading the IAT slot the call goes through and comparing it with GetProcAddress:
;
;       status = RtlUnicodeToUTF8N(cbMultiByte ? lpMultiByteStr : NULL, cbMultiByte, &produced,
;                                  lpWideCharStr, (ULONG)(cchWideChar * 2))
;
; so cbMultiByte == 0 is the MEASURING MODE and the destination pointer is not even read. Changes
; 016 and 034 shipped without that mode and FAULTED on it; this file has it from the first commit,
; and it is a vectorised counting pass rather than the character-at-a-time walk 016 first shipped.
;
; A NEGATIVE cchWideChar -- ANY negative value, not only -1, proved for -2, -1000 and INT_MIN --
; means the string is NUL-terminated, and the count becomes length+1, so the terminator is converted
; and counted.
;
; The three things MSDN gets wrong about this function, all proved both ways:
;   * lpDefaultChar is IGNORED for CP_UTF8. MSDN says a non-NULL one fails the call. The shipped
;     code checks that only for CP_UTF7 (65000).
;   * lpUsedDefaultChar is ACCEPTED for CP_UTF8 and WRITTEN: *p = (a lone surrogate was replaced).
;     MSDN says it must be NULL.
;   * dwFlags accepts exactly 0x000006F0 for CP_UTF8 -- WC_DISCARDNS|WC_SEPCHARS|WC_DEFAULTCHAR|
;     WC_ERR_INVALID_CHARS|WC_COMPOSITECHECK|WC_NO_BEST_FIT_CHARS -- and rejects every other bit
;     with ERROR_INVALID_FLAGS. Only WC_ERR_INVALID_CHARS does anything: a lone surrogate then fails
;     the call with ERROR_NO_UNICODE_TRANSLATION (1113) AFTER its U+FFFD bytes have already been
;     written into the caller's buffer. All five of those flags tail-call here.
;
; On success the last error is left exactly as the caller had it -- no SetLastError(0) -- and
; correctness.c checks that on every one of its cases by writing a sentinel before each call.
;
; STATUS_SOME_NOT_MAPPED is not tracked here, and that is a consequence of the dispatch boundary
; rather than a shortcut. It is observable only through lpUsedDefaultChar or WC_ERR_INVALID_CHARS,
; and both of those tail-call. So the lone-surrogate path costs one store less than change 016's.
;
; =================================================================================================
; THE ENCODER is change 016's, which is already proved bit-exact against ntdll!RtlUnicodeToUTF8N and
; landed at 2.46x over 24 input classes. Reusing it rather than rewriting it is the whole reason
; this change is tractable: the malformed-input rules, the exact overflow semantics and the four
; vector blocks were reverse-engineered there and are not re-derived here.
;
;   16-wide ASCII       16 characters all < 0x80            -> 16 bytes, vpackuswb + vpermq
;   one-or-two-byte     8 characters all < 0x800            -> one VPSHUFB, indexed by a 1-bit-per-
;                                                              character length mask
;   general BMP         8 non-surrogate characters          -> two VPSHUFBs, 2 bits per character
;   surrogate pairs     4 valid high-low pairs              -> 16 bytes, no table at all
;   scalar window       everything else, 16 characters at a time before the blocks are probed again
;
; Both packing tables and the shift table are generated by rept/if at assembly time, so the source
; states the rule rather than 4096 pasted numbers, and correctness.c checks the assembler's
; arithmetic against the same rule written in C.
;
; Two disciplines carried over from 016 that are load-bearing here and easy to lose:
;
;   Exactly `produced` bytes are written and not one more. The packing blocks compute sixteen bytes
;   and want fewer; storing all sixteen puts zeros in the caller's buffer past the end of the string
;   and the shipped export leaves those bytes alone. Change 268 found that in 016 after a year.
;   STOREX writes exactly L bytes as two OVERLAPPING stores and never reads the destination -- the
;   read-modify-write version was measured and cost 2.9x on three-byte input, because a partially
;   overlapping load cannot be forwarded from the store buffer. correctness.c here compares the
;   whole destination window, past cbMultiByte, on every case.
;
;   a scalar walk must not re-enter a vector loop (change 263's rule). Once the blocks have failed,
;   sixteen characters are encoded one at a time before they are probed again, so the probe is
;   amortised over a cache line of input instead of over one character. Broken, this function is
;   0.21x on alternating input while its table says 2.6x. The same window is in the counting pass.
;
; Page safety. a wide read happens only when that many characters of the caller's declared source
; remain: the 32-byte load needs 16 characters left, the 16-byte loads need 8, and both counts are
; checked against cchWideChar before the load issues. The destination is guarded the same way --
; every block compares dstPos + (the most it can store) against cbMultiByte first -- so nothing is
; ever written at or past cbMultiByte. correctness.c puts a PAGE_NOACCESS page immediately after the
; source AND after the destination and sweeps every length against it.
;
; The one read that is not bounded by a count is the NUL scan for a negative cchWideChar, and it is
; page-safe the other way: it aligns DOWN to 32 bytes and masks off the bytes before the string, so
; every load lies inside a page the string already occupies. A byte-misaligned wchar_t* cannot be
; scanned that way at all -- the word lanes would not line up with the caller's characters -- so an
; odd pointer takes a scalar scan, which correctness.c drives explicitly.
;
; Isa: AVX2 + BMI1 (tzcnt) + BMI2 (pdep) + POPCNT. No AVX-512: this is the implementation of record
; and it has to run on benches #1 and #2 as well, which have none. See RESULTS.md for what VBMI2
; would buy and why it is not here.
; =================================================================================================

EXTERN  __imp_WideCharToMultiByte:QWORD
EXTERN  __imp_SetLastError:QWORD

OVF     TEXTEQU <dword ptr [rsp+32]>               ; the conversion overflowed
WINEND  TEXTEQU <dword ptr [rsp+36]>               ; how far the scalar window runs

.const
; the 256-bit constants first (.const is PARA-aligned, so 16 is the most MASM will give a
; segment-relative ALIGN here; every ymm load below is VEX-encoded and unaligned-capable)
ALIGN 16
CFF80y  DW      16 dup(0FF80h)
ALIGN 16
CF800y  DW      16 dup(0F800h)
ALIGN 16
CD800y  DW      16 dup(0D800h)
ALIGN 16
CZEROy  DW      16 dup(0)
ALIGN 16
CFC00y  DW      16 dup(0FC00h)
ALIGN 16
CPAIRy  DW      8 dup(0D800h, 0DC00h)              ; eight high-low pairs, for the counting pass
ALIGN 16
C3Fd    DD      8 dup(3Fh)
CFFd    DD      8 dup(0FFh)
C7Fd    DD      8 dup(7Fh)
C7FFd   DD      8 dup(7FFh)
C4000d  DD      8 dup(4000h)
CFORMd  DD      8 dup(008080E0h)                   ; the 0xE0 / 0x80 / 0x80 markers, in place
; and the 128-bit ones
ALIGN 16
CFF80x  DW      8 dup(0FF80h)
CF800x  DW      8 dup(0F800h)
CD800x  DW      8 dup(0D800h)
CFC00x  DW      8 dup(0FC00h)
CPAIRx  DW      4 dup(0D800h, 0DC00h)              ; what a HIGH then a LOW surrogate look like
CMUL4x  DW      4 dup(0400h, 0001h)                ; (hi-D800)*0x400 + (lo-DC00), via VPMADDWD
C10000d DD      4 dup(00010000h)
CFORM4d DD      4 dup(808080F0h)                   ; the 0xF0 / 0x80 / 0x80 / 0x80 markers
C3Fw    DW      8 dup(003Fh)
C80w    DW      8 dup(0080h)
C80C0w  DW      8 dup(080C0h)                      ; the 0xC0 lead / 0x80 continuation markers

; -------------------------------------------------------------------------------------------------
; The one-or-two-byte packing table. Indexed by the eight-bit mask of which of eight characters are
; single bytes; entry k is the VPSHUFB that drops the continuation byte of each single-byte lane.
; A lane holds the two-byte form as a 16-bit word, little-endian, so byte 2i is the lead and 2i+1
; the continuation; a single-byte character has the character itself in byte 2i and a zero above it.
; -------------------------------------------------------------------------------------------------
ALIGN 16
LAT2 LABEL BYTE
PKI = 0
REPT 256
    PKN = 0
    PKJ = 0
    REPT 8
        IF (PKI SHR PKJ) AND 1
            DB (2*PKJ)                             ; one byte: the character itself
            PKN = PKN + 1
        ELSE
            DB (2*PKJ)                             ; two bytes: lead then continuation
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

ALIGN 16
LAT2L LABEL BYTE
PKI = 0
REPT 256
    PKN = 0
    PKJ = 0
    REPT 8
        IF (PKI SHR PKJ) AND 1
            PKN = PKN + 1
        ELSE
            PKN = PKN + 2
        ENDIF
        PKJ = PKJ + 1
    ENDM
    DB PKN
    PKI = PKI + 1
ENDM

; -------------------------------------------------------------------------------------------------
; The shift table. Entry k shuffles byte k+i down to position i, so the last eight bytes of a vector
; can be brought to where an 8-byte store will emit them. That is what lets exactly L bytes go out
; as two overlapping stores with no read of the destination.
; -------------------------------------------------------------------------------------------------
ALIGN 16
SHIFTR LABEL BYTE
PKI = 0
REPT 17
    PKJ = 0
    REPT 16
        IF (PKI + PKJ) LT 16
            DB PKI + PKJ
        ELSE
            DB 80h
        ENDIF
        PKJ = PKJ + 1
    ENDM
    PKI = PKI + 1
ENDM

; -------------------------------------------------------------------------------------------------
; The general packing table. Four bytes are laid out per 32-bit lane:
;       [0] 0xE0|(c>>12)   [1] 0x80|((c>>6)&0x3F)   [2] 0x80|(c&0x3F)   [3] c
; and a character of length 1 takes lane byte 3, length 2 takes 1 and 2, length 3 takes 0, 1 and 2.
; Byte 3 exists because 0x80|(c&0x3F) is NOT c for an ASCII character above 0x3F -- it drops bit 6 --
; so the one-byte form has to be carried separately rather than masked out of the three-byte one.
; That was a real bug in change 016's first draft and it would have mangled every capital letter in
; a string that also contained a non-ASCII character.
; -------------------------------------------------------------------------------------------------
ALIGN 16
PACK3 LABEL BYTE
PKI = 0
REPT 256
    PKN = 0
    PKJ = 0
    REPT 4
        PKC = (PKI SHR (2*PKJ)) AND 3
        IF PKC EQ 0
            DB (4*PKJ)+3                           ; one byte: the character itself
            PKN = PKN + 1
        ELSEIF PKC EQ 1
            DB (4*PKJ)+1                           ; two bytes: 0xC0|(c>>6), 0x80|(c&0x3F)
            DB (4*PKJ)+2
            PKN = PKN + 2
        ELSEIF PKC EQ 2
            DB (4*PKJ)+0                           ; three bytes
            DB (4*PKJ)+1
            DB (4*PKJ)+2
            PKN = PKN + 3
        ENDIF
        PKJ = PKJ + 1
    ENDM
    REPT 16 - PKN
        DB 80h                                     ; VPSHUFB writes a zero for a negative index
    ENDM
    PKI = PKI + 1
ENDM

ALIGN 16
PACK3L LABEL BYTE
PKI = 0
REPT 256
    PKN = 0
    PKJ = 0
    REPT 4
        PKC = (PKI SHR (2*PKJ)) AND 3
        PKN = PKN + PKC + 1
        PKJ = PKJ + 1
    ENDM
    DB PKN
    PKI = PKI + 1
ENDM

PUBLIC wia_wc2mb_pack_table
PUBLIC wia_wc2mb_pack_len
PUBLIC wia_wc2mb_lat_table
PUBLIC wia_wc2mb_lat_len
PUBLIC wia_wc2mb_shift_table
PUBLIC wia_wc2mb_fallback

; Storex -- write exactly eax bytes of `xdata` at [rbx + r15], using `xtmp` and edx as scratch and
; Rcx as the base of shiftr. Two overlapping stores, no read of the destination. Eax is left alone
; so the caller can advance the position with it.
STOREX  MACRO xdata, xtmp
        LOCAL   stsmall, stfin
        cmp       eax, 8
        jb        stsmall
        vmovq     qword ptr [rbx + r15], xdata
        lea       edx, [rax - 8]
        shl       edx, 4
        vpshufb   xtmp, xdata, xmmword ptr [rcx + rdx]
        lea       rdx, [r15 + rax - 8]
        vmovq     qword ptr [rbx + rdx], xtmp
        jmp       stfin
stsmall:vmovd     dword ptr [rbx + r15], xdata
        lea       edx, [rax - 4]
        shl       edx, 4
        vpshufb   xtmp, xdata, xmmword ptr [rcx + rdx]
        lea       rdx, [r15 + rax - 4]
        vmovd     dword ptr [rbx + rdx], xtmp
stfin:
        ENDM

.code

; correctness.c calls these to check the assembler-generated tables against the same rule in C, and
; to prove that the tail call really does land on kernelbase!WideCharToMultiByte.
wia_wc2mb_pack_table PROC
        lea       rax, [PACK3]
        ret
wia_wc2mb_pack_table ENDP
wia_wc2mb_pack_len PROC
        lea       rax, [PACK3L]
        ret
wia_wc2mb_pack_len ENDP
wia_wc2mb_lat_table PROC
        lea       rax, [LAT2]
        ret
wia_wc2mb_lat_table ENDP
wia_wc2mb_lat_len PROC
        lea       rax, [LAT2L]
        ret
wia_wc2mb_lat_len ENDP
wia_wc2mb_shift_table PROC
        lea       rax, [SHIFTR]
        ret
wia_wc2mb_shift_table ENDP
wia_wc2mb_fallback PROC
        mov       rax, qword ptr [__imp_WideCharToMultiByte]
        ret
wia_wc2mb_fallback ENDP


wia_wc2mb PROC
; -------------------------------------------------------------------------------------------------
; THE DISPATCH. Nothing has been pushed yet, so `wc_tail` is a true tail call: rcx/rdx/r8/r9 still
; hold the caller's arguments, [rsp+28h..] still holds the stack ones, and [rsp] is still the
; caller's return address. Only rax, r10 and r11 are touched here, and all three are volatile.
; -------------------------------------------------------------------------------------------------
        cmp       ecx, 0FDE9h                       ; CP_UTF8 == 65001
        jne       wc_tail
        test      edx, edx                          ; dwFlags must be exactly 0
        jnz       wc_tail
        mov       rax, qword ptr [rsp + 38h]        ; lpDefaultChar
        or        rax, qword ptr [rsp + 40h]        ; lpUsedDefaultChar
        jnz       wc_tail
        ; the shipped argument checks, in the shipped order. Each failure is an error the real
        ; export reports through RtlSetLastWin32Error, so each failure hands the whole call back.
        test      r9d, r9d
        jz        wc_tail                           ; cchWideChar == 0
        mov       r10d, dword ptr [rsp + 30h]       ; cbMultiByte
        test      r10d, r10d
        js        wc_tail                           ; cbMultiByte < 0
        test      r8, r8
        jz        wc_tail                           ; lpWideCharStr == NULL
        mov       r11, qword ptr [rsp + 28h]        ; lpMultiByteStr
        test      r10d, r10d
        jz        wc_len                            ; cbMultiByte == 0: the destination is not read
        test      r11, r11
        jz        wc_tail                           ; NULL destination with a non-zero size
        cmp       r11, r8
        je        wc_tail                           ; EXACT pointer equality -- overlap is allowed

wc_len:
        test      r9d, r9d
        jns       wc_have                           ; an explicit, positive count
; ------------------------------------------------------------------------------------------------
; a negative cchWideChar means nul-terminated, and the terminator is converted, so the count is
; length + 1. Any negative value does this, not only -1 (proved for -2, -1000 and INT_MIN).
;
; The shipped code walks this scalar, sixteen words unrolled. It is worth vectorising because
; cchWideChar = -1 is the commonest way this function is called, but only with the page discipline:
; align DOWN to 32 bytes and mask off the bytes before the string, so every load lies inside a page
; the string already occupies. A 32-byte load at a 32-aligned address never crosses a 4 KB page.
;
; An odd wchar_t* cannot take that path at all. The aligned block's 16-bit lanes are even-addressed,
; so for a byte-misaligned string they straddle the caller's characters and a terminator would be
; missed or invented. It is one `test` to send those to a scalar scan, and correctness.c drives it.
; ------------------------------------------------------------------------------------------------
        test      r8b, 1
        jnz       wc_scan_odd
        vpxor     xmm1, xmm1, xmm1
        mov       rax, r8
        and       rax, -32                          ; the 32-byte block the string starts in
        mov       ecx, r8d
        and       ecx, 31                           ; its byte offset inside that block
        vpcmpeqw  ymm0, ymm1, ymmword ptr [rax]
        vpmovmskb r9d, ymm0
        shr       r9d, cl                           ; drop the bytes before the string
        test      r9d, r9d
        jnz       wc_scan_first
ALIGN 16
wc_scan_next:
        add       rax, 32
        vpcmpeqw  ymm0, ymm1, ymmword ptr [rax]
        vpmovmskb r9d, ymm0
        test      r9d, r9d
        jz        wc_scan_next
        tzcnt     r9d, r9d
        add       rax, r9
        sub       rax, r8                           ; bytes from the string start to the terminator
        shr       rax, 1
        lea       r9d, [rax + 1]                    ; ... and the terminator is INCLUDED
        vzeroupper
        jmp       wc_have
wc_scan_first:
        tzcnt     r9d, r9d                          ; byte offset of the terminator from the start
        shr       r9d, 1
        inc       r9d
        vzeroupper
        jmp       wc_have
wc_scan_odd:
        xor       eax, eax
wc_scan_odd_l:
        cmp       word ptr [r8 + rax*2], 0
        je        wc_scan_odd_d
        inc       eax
        jmp       wc_scan_odd_l
wc_scan_odd_d:
        lea       r9d, [rax + 1]

wc_have:
        test      r10d, r10d
        jz        wc_measure                        ; cbMultiByte == 0: nothing is written at all

; ------------------------------------------------------------------------------------------------
; An overlapping destination tail-calls, and this is not caution -- correctness.c caught a real
; divergence here and it is the reason the check exists.
;
; The shipped code rejects only EXACT pointer equality (proved), so a destination that merely
; OVERLAPS the source is accepted and converted. What comes out then depends entirely on the order
; the implementation reads and writes in: a walk that reads one character and writes its bytes
; before reading the next sees its own output when the destination is ahead of the read cursor,
; while a block that reads sixteen characters and then stores does not. Both are "correct" UTF-8 of
; something; they are not the same bytes. With lpMultiByteStr = (char*)lpWideCharStr + 2 and eight
; ASCII characters, ours and the shipped export produced different buffers -- same return value,
; same last error, different text, which is exactly the shape of bug that ships.
;
; There is no way to be bit-exact here except by being the same implementation, so overlapping
; calls become the same implementation: they are handed to the real export.
;
; The count is handed over RESOLVED. If the caller passed a negative cchWideChar the scan above has
; already replaced it with length+1, and converting exactly that many characters is what the shipped
; code would do with the negative value anyway -- it would simply rescan the same, still-unmodified,
; string. CodePage and dwFlags are restored by value because the dispatch above proved what they
; are; nothing else has been touched, and nothing has been written.
; ------------------------------------------------------------------------------------------------
        mov       eax, r9d                          ; the character count, zero-extended
        lea       rax, [r8 + rax*2]                 ; one past the last source byte
        cmp       r11, rax
        jae       wc_convert                        ; the destination starts after the source ends
        mov       eax, r10d                         ; cbMultiByte, zero-extended
        add       rax, r11                          ; one past the last destination byte
        cmp       r8, rax
        jae       wc_convert                        ; the source starts after the destination ends
        mov       ecx, 0FDE9h                       ; they overlap: hand the whole call over
        xor       edx, edx
        jmp       qword ptr [__imp_WideCharToMultiByte]

; =================================================================================================
; THE CONVERSION. r8 = src, r9d = characters, r10d = cbMultiByte, r11 = dst, and the destination is
; known to be disjoint from the source.
; =================================================================================================
wc_convert:
        push      rbx
        push      rsi
        push      rdi
        push      r13
        push      r14
        push      r15
        sub       rsp, 56                           ; 32 shadow (for SetLastError) + our two locals
        mov       rbx, r11                          ; dst
        mov       edi, r10d                         ; dstMax, zero-extended
        mov       rsi, r8                           ; src
        mov       r13d, r9d                         ; n characters
        mov       OVF, 0
        xor       r14, r14                          ; srcIdx
        xor       r15, r15                          ; dstPos

ALIGN 16
mainloop:
        cmp       r14d, r13d
        jae       done
        ; ---- 16 characters, all < 0x80, with room for 16 bytes ----
        mov       r8d, r13d
        sub       r8d, r14d
        cmp       r8d, 16
        jb        ascii8
        lea       r8, [r15 + 16]
        cmp       r8, rdi
        ja        ascii8
        vmovdqu   ymm0, ymmword ptr [rsi + r14*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80y]
        vptest    ymm1, ymm1
        jnz       ascii8
        vpackuswb ymm0, ymm0, ymm0
        vpermq    ymm0, ymm0, 0D8h                  ; gather the packed bytes into the low 16
        vmovdqu   xmmword ptr [rbx + r15], xmm0
        add       r15, 16
        add       r14, 16
        jmp       mainloop

ALIGN 16
ascii8:
        ; ---- 8 characters, all < 0x80, with room for 8 bytes ----
        mov       r8d, r13d
        sub       r8d, r14d
        cmp       r8d, 8
        jb        scalar_win                        ; fewer than 8 left: no block can run
        lea       r8, [r15 + 8]
        cmp       r8, rdi
        ja        lat8
        vmovdqu   xmm0, xmmword ptr [rsi + r14*2]
        vpand     xmm1, xmm0, xmmword ptr [CFF80x]
        vptest    xmm1, xmm1
        jnz       lat8
        vpackuswb xmm0, xmm0, xmm0
        vmovq     qword ptr [rbx + r15], xmm0
        add       r15, 8
        add       r14, 8
        jmp       mainloop

; -------------------------------------------------------------------------------------------------
; The one-or-two-byte block: eight characters under 0x800, in about eighteen instructions. This is
; the block that carries `mixed` input -- ASCII spaces and punctuation alternating with two-byte
; letters, which is what Hebrew, Greek, Cyrillic or accented Latin prose actually looks like, and
; the commonest non-ASCII input there is. The 0xF800 test rejects surrogates for free.
; -------------------------------------------------------------------------------------------------
ALIGN 16
lat8:
        lea       r8, [r15 + 16]                    ; eight characters produce at most 16 bytes
        cmp       r8, rdi
        ja        bmp8
        vmovdqu   xmm0, xmmword ptr [rsi + r14*2]
        vpand     xmm1, xmm0, xmmword ptr [CF800x]
        vptest    xmm1, xmm1
        jnz       bmp8                              ; something needs three bytes, or is a surrogate

        vpand     xmm2, xmm0, xmmword ptr [C3Fw]
        vpsllw    xmm2, xmm2, 8                     ; the continuation byte, in the high half
        vpsrlw    xmm3, xmm0, 6
        vpor      xmm2, xmm2, xmm3                  ; the lead byte, in the low half
        vpor      xmm2, xmm2, xmmword ptr [C80C0w]
        vmovdqu   xmm4, xmmword ptr [C80w]
        vpcmpgtw  xmm4, xmm4, xmm0                  ; -1 where the character is ONE byte
        vpblendvb xmm2, xmm2, xmm0, xmm4            ; ... and there the lane is the character
        vpacksswb xmm5, xmm4, xmm4
        vpmovmskb r8d, xmm5
        and       r8d, 0FFh                         ; one bit per character
        lea       r10, [LAT2]
        lea       r11, [LAT2L]
        movzx     eax, byte ptr [r11 + r8]
        shl       r8d, 4
        vpshufb   xmm2, xmm2, xmmword ptr [r10 + r8]
        lea       rcx, [SHIFTR]
        STOREX    xmm2, xmm3                        ; exactly EAX bytes; this block never produces
        add       r15, rax                          ; fewer than eight, so that branch is settled
        add       r14, 8
        jmp       mainloop

; -------------------------------------------------------------------------------------------------
; The general bmp block: eight characters of any length at once. Each character is expanded into its
; own 32-bit lane holding all three candidate bytes plus the character itself, two comparisons give
; each character's LENGTH, PDEP packs those lengths into a two-bit-per-character index, and one
; VPSHUFB per four characters compacts the lanes. Nothing is branched on per character.
;
; The surrogate test comes before the room test, and the order is deliberate. Written the other way
; round, this block's 32-byte guard would also be guarding the surrogate block below, whose own
; 16-byte guard could then never fail -- a check that cannot fail reads like a safeguard and is not
; one. In change 016 that was the single mutant of nineteen the gate could not catch.
;
; The destination is guarded by 32 Bytes, not by the 24 the block can produce: each half stores
; exactly what it wants but the second half begins at most 12 bytes in, so 32 is one comparison with
; no arithmetic left to get wrong.
; -------------------------------------------------------------------------------------------------
ALIGN 16
bmp8:
        vmovdqu   xmm0, xmmword ptr [rsi + r14*2]   ; eight source characters
        vpand     xmm1, xmm0, xmmword ptr [CF800x]
        vpcmpeqw  xmm1, xmm1, xmmword ptr [CD800x]
        vpmovmskb r8d, xmm1
        test      r8d, r8d
        jnz       surr8                             ; a surrogate: a different block, below
        lea       r8, [r15 + 32]                    ; eight characters are known to be there:
        cmp       r8, rdi                           ; the 8-wide block above just checked
        ja        scalar_win

        vpmovzxwd ymm1, xmm0                        ; c, one per 32-bit lane
        vpsrld    ymm2, ymm1, 12                    ; [0] = c >> 12
        vpsrld    ymm3, ymm1, 6
        vpand     ymm3, ymm3, ymmword ptr [C3Fd]
        vpslld    ymm3, ymm3, 8                     ; [1] = (c >> 6) & 0x3F
        vpor      ymm2, ymm2, ymm3
        vpand     ymm3, ymm1, ymmword ptr [C3Fd]
        vpslld    ymm3, ymm3, 16                    ; [2] = c & 0x3F
        vpor      ymm2, ymm2, ymm3
        vpand     ymm3, ymm1, ymmword ptr [CFFd]
        vpslld    ymm3, ymm3, 24                    ; [3] = c, for the one-byte form
        vpor      ymm2, ymm2, ymm3
        vpor      ymm2, ymm2, ymmword ptr [CFORMd]  ; the 0xE0 / 0x80 / 0x80 markers

        vpcmpgtd  ymm3, ymm1, ymmword ptr [C7Fd]    ; two bytes or more
        vpcmpgtd  ymm4, ymm1, ymmword ptr [C7FFd]   ; three bytes
        vpandn    ymm5, ymm4, ymm3                  ; exactly two bytes
        vpand     ymm5, ymm5, ymmword ptr [C4000d]
        vpor      ymm2, ymm2, ymm5                  ; ... whose lead is 0xC0|(c>>6), not 0x80|

        lea       r10, [PACK3]
        lea       r11, [PACK3L]
        vmovmskps r8d, ymm3
        vmovmskps r9d, ymm4
        mov       eax, 5555h
        pdep      r8d, r8d, eax                     ; one bit per character -> two bits per
        pdep      r9d, r9d, eax                     ; character, and the two sum to length-1
        add       r8d, r9d
        movzx     r9d, r8b                          ; characters 0..3
        shr       r8d, 8                            ; characters 4..7

        lea       rcx, [SHIFTR]
        movzx     eax, byte ptr [r11 + r9]          ; how many bytes that half produces
        shl       r9d, 4
        vpshufb   xmm3, xmm2, xmmword ptr [r10 + r9]
        STOREX    xmm3, xmm1                        ; BOTH halves store exactly: the second writes
        add       r15, rax                          ; only its OWN length too, so a surplus zero
        vextracti128 xmm4, ymm2, 1                  ; left by the first would simply survive
        movzx     eax, byte ptr [r11 + r8]
        shl       r8d, 4
        vpshufb   xmm4, xmm4, xmmword ptr [r10 + r8]
        STOREX    xmm4, xmm1
        add       r15, rax

        add       r14, 8
        jmp       mainloop

; -------------------------------------------------------------------------------------------------
; The surrogate-pair block: four pairs, sixteen bytes, no shuffle. Every valid pair is exactly two
; characters in and four bytes out, so four of them are sixteen bytes with no length code at all.
; Masking with 0xFC00 and comparing against the alternating [D800, DC00] pattern establishes in one
; Vpcmpeqw that all eight characters are surrogates and that they alternate high-low starting here.
; The arithmetic is one VPMADDWD: subtracting that pattern leaves each half in 0..0x3FF, and
; multiplying by [0x400, 1] and summing adjacent pairs IS (hi-0xD800)*0x400 + (lo-0xDC00).
; -------------------------------------------------------------------------------------------------
surr8:
        lea       r8, [r15 + 16]
        cmp       r8, rdi
        ja        scalar_win
        vpand     xmm1, xmm0, xmmword ptr [CFC00x]
        vpcmpeqw  xmm1, xmm1, xmmword ptr [CPAIRx]
        vpmovmskb r8d, xmm1
        cmp       r8d, 0FFFFh
        jne       scalar_win                        ; not four clean pairs starting here

        vpsubw    xmm0, xmm0, xmmword ptr [CPAIRx]
        vpmaddwd  xmm0, xmm0, xmmword ptr [CMUL4x]
        vpaddd    xmm0, xmm0, xmmword ptr [C10000d] ; the code point
        vpsrld    xmm1, xmm0, 18
        vpsrld    xmm2, xmm0, 12
        vpand     xmm2, xmm2, xmmword ptr [C3Fd]
        vpslld    xmm2, xmm2, 8
        vpor      xmm1, xmm1, xmm2
        vpsrld    xmm2, xmm0, 6
        vpand     xmm2, xmm2, xmmword ptr [C3Fd]
        vpslld    xmm2, xmm2, 16
        vpor      xmm1, xmm1, xmm2
        vpand     xmm2, xmm0, xmmword ptr [C3Fd]
        vpslld    xmm2, xmm2, 24
        vpor      xmm1, xmm1, xmm2
        vpor      xmm1, xmm1, xmmword ptr [CFORM4d]
        vmovdqu   xmmword ptr [rbx + r15], xmm1
        add       r15, 16
        add       r14, 8
        jmp       mainloop

; -------------------------------------------------------------------------------------------------
; The scalar window. Once the blocks have failed, sixteen characters are encoded one at a time
; before the blocks are probed again, so the probe is amortised over a cache line of input rather
; than over one character. Without it -- change 263's rule broken -- alternating input costs two
; vector loads and two tests PER CHARACTER and this function runs at 0.21x while its table says 2.6x.
;
; This is also where the exact overflow semantics live: the first sequence that does not fit stops
; the conversion, everything written so far stays written, and the call reports
; ERROR_INSUFFICIENT_BUFFER with a return value of 0.
; -------------------------------------------------------------------------------------------------
scalar_win:
        lea       r8d, [r14 + 16]
        mov       WINEND, r8d

scalar_char:
        movzx     eax, word ptr [rsi + r14*2]       ; c
        cmp       eax, 80h
        jb        emit1
        cmp       eax, 800h
        jb        emit2
        cmp       eax, 0D800h
        jb        emit3
        cmp       eax, 0DC00h
        jb        high_surr
        cmp       eax, 0E000h
        jb        lone
        jmp       emit3

emit1:
        lea       r8, [r15 + 1]
        cmp       r8, rdi
        jbe       e1w
        mov       OVF, 1
        jmp       done
e1w:    mov       byte ptr [rbx + r15], al
        inc       r15
        inc       r14
        jmp       scalar_next

emit2:
        lea       r8, [r15 + 2]
        cmp       r8, rdi
        jbe       e2w
        mov       OVF, 1
        jmp       done
e2w:    mov       r9d, eax
        shr       r9d, 6
        or        r9d, 0C0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
        add       r15, 2
        inc       r14
        jmp       scalar_next

emit3:
        lea       r8, [r15 + 3]
        cmp       r8, rdi
        jbe       e3w
        mov       OVF, 1
        jmp       done
e3w:    mov       r9d, eax
        shr       r9d, 12
        or        r9d, 0E0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        shr       r9d, 6
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 2], r9b
        add       r15, 3
        inc       r14
        jmp       scalar_next

high_surr:
        lea       r8d, [r14 + 1]
        cmp       r8d, r13d
        jae       lone
        movzx     r9d, word ptr [rsi + r8*2]        ; the next character
        cmp       r9d, 0DC00h
        jb        lone
        cmp       r9d, 0E000h
        jae       lone
        sub       eax, 0D800h                       ; a valid pair -> the code point, in eax
        shl       eax, 10
        sub       r9d, 0DC00h
        add       eax, r9d
        add       eax, 10000h
        lea       r8, [r15 + 4]
        cmp       r8, rdi
        jbe       e4w
        mov       OVF, 1
        jmp       done
e4w:    mov       r9d, eax
        shr       r9d, 18
        or        r9d, 0F0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        shr       r9d, 12
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
        mov       r9d, eax
        shr       r9d, 6
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 2], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 3], r9b
        add       r15, 4
        add       r14, 2                            ; two characters consumed
        jmp       scalar_next

; a lone surrogate becomes u+fffd. Change 016 also records STATUS_SOME_NOT_MAPPED here; this
; function does not, because within the dispatch boundary above there is no way to observe it --
; it reaches the caller only through lpUsedDefaultChar or WC_ERR_INVALID_CHARS, and both of those
; tail-call. One store fewer, on a path that is entirely scalar anyway.
lone:
        lea       r8, [r15 + 3]
        cmp       r8, rdi
        jbe       ffw
        mov       OVF, 1
        jmp       done
ffw:    mov       byte ptr [rbx + r15], 0EFh
        mov       byte ptr [rbx + r15 + 1], 0BFh
        mov       byte ptr [rbx + r15 + 2], 0BDh
        add       r15, 3
        inc       r14
        jmp       scalar_next

scalar_next:
        cmp       r14d, r13d
        jae       done                              ; the source is finished
        cmp       r14d, WINEND
        jb        scalar_char                       ; still inside the window: stay scalar
        jmp       mainloop                          ; the window is spent: probe the blocks again

; -------------------------------------------------------------------------------------------------
; The return value, and the ONE error this function reports itself.
; -------------------------------------------------------------------------------------------------
done:
        vzeroupper
        cmp       OVF, 0
        jne       wc_c_small
        mov       eax, r15d
        test      eax, eax
        jz        wc_c_zero                         ; produced == 0: the shipped code sets the last
        cmp       eax, 7FFFFFFFh                    ; error to 0 here. Unreachable for CP_UTF8 --
        ja        wc_c_big                          ; every character makes at least one byte --
wc_c_ret:                                           ; but the shipped code has the branch, so does
        add       rsp, 56                           ; this one.
        pop       r15
        pop       r14
        pop       r13
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wc_c_small:
        mov       ecx, 7Ah                          ; ERROR_INSUFFICIENT_BUFFER -- and the partial
        jmp       wc_c_err                          ; output STAYS in the caller's buffer
wc_c_zero:
        xor       ecx, ecx
        jmp       wc_c_err
wc_c_big:
        mov       ecx, 57h                          ; ERROR_INVALID_PARAMETER
wc_c_err:
        call      qword ptr [__imp_SetLastError]    ; the real one: it has two debug hooks in it
        xor       eax, eax                          ; that a bare `mov gs:[68h]` would skip
        jmp       wc_c_ret

; =================================================================================================
; The measuring mode: cbMultiByte == 0 asks how many bytes the output would need, and the
; destination pointer is not read. r8 = src, r9d = characters.
;
; This is a leaf -- no pushes, nothing but the volatile registers and the caller's shadow space,
; which holds the scalar window's end.
;
; It is vectorised, and it has to be. Change 016's first measuring mode walked one character at a
; time because nothing was expected to call it in a loop; then change 268 called it on every
; allocating conversion and that row came out at 0.47x. Here it is the FIRST of the two calls in the
; measure-then-convert idiom that every caller of this function uses, so it is on the hot path by
; construction -- discovery/desktop-startup-timings.md times the shipped one at 476 ns for 4095
; characters, which is a quarter of the cost of the conversion it precedes.
;
; The counting rule for sixteen characters with no surrogate among them is exact arithmetic, not a
; walk: bytes = 3*16 - (how many are < 0x800) - (how many are < 0x80), because a character under
; 0x80 is counted out of both sets and lands on 1, one under 0x800 out of one set and lands on 2,
; and anything else stays at 3. Two VPCMPEQWs and two POPCNTs give both counts. Surrogates are the
; only irregular case -- a valid pair is four bytes from TWO characters -- and they go to the scalar
; walk, which has its own window so it never re-enters the block per character.
; =================================================================================================
ALIGN 16
wc_measure:
        mov       r10d, r9d                         ; n characters
        mov       r9, r8                            ; src
        xor       r11d, r11d                        ; bytes so far
        xor       ecx, ecx                          ; index
        jmp       wc_m_test

ALIGN 16
wc_m_fast:
        vmovdqu   ymm0, ymmword ptr [r9 + rcx*2]    ; sixteen characters
        vpand     ymm4, ymm0, ymmword ptr [CFF80y]
        vptest    ymm4, ymm4
        jz        wc_m_a16                          ; all sixteen are one byte each
        vpand     ymm1, ymm0, ymmword ptr [CF800y]
        vpcmpeqw  ymm2, ymm1, ymmword ptr [CD800y]
        vptest    ymm2, ymm2
        jnz       wc_m_surr                         ; a surrogate: try them as eight PAIRS first
        vpcmpeqw  ymm3, ymm1, ymmword ptr [CZEROy]  ; -1 where the character is under 0x800
        vpcmpeqw  ymm4, ymm4, ymmword ptr [CZEROy]  ; -1 where the character is under 0x80
        vpmovmskb eax, ymm3
        vpmovmskb edx, ymm4
        popcnt    eax, eax                          ; two mask bits per character, so both of
        popcnt    edx, edx                          ; these are twice the count
        add       eax, edx
        shr       eax, 1
        add       r11d, 48                          ; 3 * 16 ...
        sub       r11d, eax                         ; ... less one for each set a character is in
        add       ecx, 16
        jmp       wc_m_test
ALIGN 16
wc_m_a16:
        add       r11d, 16
        add       ecx, 16
        jmp       wc_m_test

; Sixteen characters that are eight surrogate pairs. The counting block above rejects surrogates
; wholesale, which left supplementary-plane text -- every emoji there is -- counting one character
; at a time, and that measured a TIE against ntdll's own sizing pass (0.94x-1.03x) while every other
; class was 3x to 8x. Pairs are worth their own test for the same reason change 016's converter gave
; them their own block: they are irregular in the other block's terms and perfectly regular in their
; own. Masking with 0xFC00 and comparing against the alternating [D800, DC00] pattern says in one
; Vpcmpeqw that all sixteen are surrogates and that they alternate high-low starting here, and eight
; valid pairs are exactly thirty-two bytes with nothing to count.
wc_m_surr:
        vpand     ymm1, ymm0, ymmword ptr [CFC00y]
        vpcmpeqw  ymm1, ymm1, ymmword ptr [CPAIRy]
        vpmovmskb eax, ymm1
        cmp       eax, -1
        jne       wc_m_win                          ; not eight clean pairs starting here
        add       r11d, 32
        add       ecx, 16
        jmp       wc_m_test

wc_m_win:
        lea       eax, [rcx + 16]
        mov       dword ptr [rsp + 8], eax          ; the window, in the caller's shadow space
wc_m_loop:
        movzx     eax, word ptr [r9 + rcx*2]
        cmp       eax, 80h
        jb        wc_m_one
        cmp       eax, 800h
        jb        wc_m_two
        mov       edx, eax
        and       edx, 0F800h
        cmp       edx, 0D800h
        jne       wc_m_three                        ; not a surrogate at all
        cmp       eax, 0DC00h
        jae       wc_m_three                        ; a LOW surrogate first is always lone: U+FFFD
        lea       edx, [rcx + 1]
        cmp       edx, r10d
        jae       wc_m_three                        ; nothing follows it
        movzx     edx, word ptr [r9 + rcx*2 + 2]
        sub       edx, 0DC00h
        cmp       edx, 400h
        jae       wc_m_three                        ; what follows is not a low surrogate
        add       r11d, 4                           ; a valid pair: four bytes, two units consumed
        add       ecx, 2
        jmp       wc_m_next
wc_m_three:
        add       r11d, 3
        inc       ecx
        jmp       wc_m_next
wc_m_two:
        add       r11d, 2
        inc       ecx
        jmp       wc_m_next
wc_m_one:
        inc       r11d
        inc       ecx
wc_m_next:
        cmp       ecx, r10d
        jae       wc_m_done
        cmp       ecx, dword ptr [rsp + 8]
        jb        wc_m_loop                         ; still inside the window: stay scalar
wc_m_test:
        mov       edx, r10d
        sub       edx, ecx
        cmp       edx, 16
        jae       wc_m_fast                         ; sixteen characters left: try them as a block
        cmp       ecx, r10d
        jb        wc_m_win
wc_m_done:
        vzeroupper
        mov       eax, r11d
        test      eax, eax
        jz        wc_m_zero
        cmp       eax, 7FFFFFFFh
        ja        wc_m_big
        ret
wc_m_zero:
        xor       ecx, ecx
        jmp       wc_m_err
wc_m_big:
        mov       ecx, 57h                          ; ERROR_INVALID_PARAMETER
wc_m_err:
        sub       rsp, 40                           ; 32 shadow + 8 to realign
        call      qword ptr [__imp_SetLastError]
        add       rsp, 40
        xor       eax, eax
        ret

; The tail call. One instruction, every argument still in place.
wc_tail:
        jmp       qword ptr [__imp_WideCharToMultiByte]

wia_wc2mb ENDP
END
