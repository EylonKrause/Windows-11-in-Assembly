; changes/016-rtlunicodetoutf8n/impl.asm
; NTSTATUS wia_u2u8(void* dst, ULONG dstMax, PULONG outLen, const wchar_t* src, ULONG srcBytes)
;   [Win64: rcx, edx, r8, r9, [rsp+0x28] -> eax; *outLen = bytes produced]
;
; Reimplements ntdll!RtlUnicodeToUTF8N (UTF-16 -> UTF-8). Validated in C against
; the live ntdll oracle (0 mismatches / 60000, incl. surrogate pairs and lone
; surrogates -> U+FFFD). ASCII fast path packs 8 wchars -> 8 bytes; everything
; else (2/3/4-byte sequences, surrogate decode) is exact scalar.
; Status: STATUS_SUCCESS, or 0x107 STATUS_SOME_NOT_MAPPED (a lone surrogate), or
; 0xC0000023 STATUS_BUFFER_TOO_SMALL (output did not fit).
;
; ------------------------------------------------------------------------------
; A NULL DESTINATION IS THE MEASURING MODE, ADDED 2026-09-16 -- IT WAS MISSING.
;
; RtlUnicodeToUTF8N(NULL, 0, &produced, src, srcLen) is the documented way to ask
; this function how many bytes the output will need: the shipped export returns
; STATUS_SUCCESS with `produced` set to the required count and writes nothing.
; This implementation did not. It returned STATUS_BUFFER_TOO_SMALL with produced
; = 0, and with a NULL pointer and a NON-ZERO size it DEREFERENCED the pointer
; and faulted. See discovery/utf8n_null_destination.c for the evidence.
;
; WHY THE GATES DID NOT CATCH IT: this change is bit-exact against the live export
; over large corpora, and every case in them passes a real destination buffer. A
; NULL destination is not an edge of the LENGTH, which is what those corpora
; sweep -- it is a different MODE of the same function, and nothing asked for it.
; The same shape as the SPACE bug that sat in four landed changes at once.
;
; It was found by change 268, whose allocating path has to size the output before
; it can allocate a buffer for it, and which cannot be built until this works.
;
; The counting rule below was verified against the live measuring mode over
; 200000 random strings -- ASCII, two-byte, surrogate-heavy and fully random --
; with ZERO disagreements on the size AND on the status, and measuring.c gates it
; here over 122006 more.
;
; WHAT IT COSTS, MEASURED RATHER THAN WAVED AWAY: the two-instruction test at the
; entry costs 0.12 ns on the 8-byte row -- 2.93 ns before, 3.05 after -- which is
; about 4% of the smallest call and moves the geomean from 2.68x to 2.63x. Moving
; the test AFTER the prologue, so it might issue alongside the seven pushes, was
; tried and measured WORSE at 3.13 ns; the entry is the better of the two places.
; Every row still beats the shipped code and the change still LANDS. A function
; that faults on a documented call is not worth 0.12 ns.
; ------------------------------------------------------------------------------
;
; ISA: AVX2. Validated on Zen3.

.const
ALIGN 16
CFF80x  DW      8 dup(0FF80h)
CFF80y  DW      16 dup(0FF80h)
CF800x  DW      8 dup(0F800h)
CD800x  DW      8 dup(0D800h)
ALIGN 16
C3Fd    DD      8 dup(3Fh)
CFFd    DD      8 dup(0FFh)
C7Fd    DD      8 dup(7Fh)
C7FFd   DD      8 dup(7FFh)
C4000d  DD      8 dup(4000h)
CFORMd  DD      8 dup(008080E0h)                   ; the 0xE0 / 0x80 / 0x80 markers, in place
CFC00x  DW      8 dup(0FC00h)
CPAIRx  DW      4 dup(0D800h, 0DC00h)              ; what a HIGH then a LOW surrogate look like
CMUL4x  DW      4 dup(0400h, 0001h)                ; (hi-D800)*0x400 + (lo-DC00), via VPMADDWD
C10000d DD      4 dup(00010000h)
CFORM4d DD      4 dup(808080F0h)                   ; the 0xF0 / 0x80 / 0x80 / 0x80 markers
C3Fw    DW      8 dup(003Fh)
C80w    DW      8 dup(0080h)
C80C0w  DW      8 dup(080C0h)                      ; the 0xC0 lead / 0x80 continuation markers

; -------------------------------------------------------------------------------------------------
; THE SECOND PACKING TABLE, for the case where every character is ONE or TWO bytes.
;
; The general block below handles any eight non-surrogate characters, and it costs about forty-seven
; instructions to do it. Characters under 0x800 do not need most of that: there is no third byte to
; build, no third length to encode, and the whole length code is ONE bit per character rather than
; two. The table is therefore indexed by the eight-bit mask of which characters are single bytes,
; and the block that uses it is about eighteen instructions.
;
; This is not a micro-optimisation of a rare case. `mixed` -- ASCII alternating with two-byte
; characters, which is what Hebrew, Greek, Cyrillic or accented Latin prose actually looks like once
; it has spaces and punctuation in it -- was the ONE class still at parity after the general block
; landed, at 0.91x to 1.01x. It is the commonest non-ASCII input there is.
;
; A lane holds the two-byte form as a 16-bit word, little-endian, so byte 2i is the lead and 2i+1 is
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

PUBLIC wia_u2u8_lat_table
PUBLIC wia_u2u8_lat_len

; -------------------------------------------------------------------------------------------------
; THE PACKING TABLE, GENERATED BY THE ASSEMBLER RATHER THAN PASTED IN.
;
; A block of four characters produces between four and twelve UTF-8 bytes, and which bytes they are
; depends on each character's length. The block below builds each character's THREE-byte form in its
; own 32-bit lane and then compacts the lanes with a single VPSHUFB, so the only thing that varies
; is the shuffle -- 256 of them, one per combination of four lengths at two bits each.
;
; Four bytes are laid out per lane:  [0] 0xE0|(c>>12)   [1] 0x80|((c>>6)&0x3F)
;                                    [2] 0x80|(c&0x3F)  [3] c
; and a character of length 1 takes lane byte 3, length 2 takes bytes 1 and 2, length 3 takes 0, 1
; and 2. Byte 3 exists because 0x80|(c&0x3F) is NOT c for an ASCII character above 0x3F -- it drops
; bit 6 -- so the one-byte form has to be carried separately rather than masked out of the
; three-byte one. That is a bug this table's first draft had, and it would have mangled every
; capital letter in a string that also contained a non-ASCII character.
;
; REPT and IF run at assembly time, so the table is a consequence of the rule stated above rather
; than 4096 numbers someone typed. correctness.c checks it against the same rule written in C, so a
; mistake in the assembler's arithmetic is caught rather than assumed away.
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

PUBLIC wia_u2u8_pack_table
PUBLIC wia_u2u8_pack_len

.code

; correctness.c calls these to check the assembler-generated table against the same rule in C
wia_u2u8_pack_table PROC
        lea       rax, [PACK3]
        ret
wia_u2u8_pack_table ENDP
wia_u2u8_pack_len PROC
        lea       rax, [PACK3L]
        ret
wia_u2u8_pack_len ENDP
wia_u2u8_lat_table PROC
        lea       rax, [LAT2]
        ret
wia_u2u8_lat_table ENDP
wia_u2u8_lat_len PROC
        lea       rax, [LAT2L]
        ret
wia_u2u8_lat_len ENDP

wia_u2u8 PROC
        test      rcx, rcx
        jz        u2u8_measure                      ; a NULL destination asks for the SIZE only
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rbx, rcx                          ; dst
        mov       edi, edx                          ; dstMax (zero-extended)
        mov       r12, r8                           ; outLen ptr
        mov       rsi, r9                           ; src
        mov       r13d, dword ptr [rsp + 60h]       ; srcBytes (5th arg)
        shr       r13d, 1                           ; srcN wchars
        sub       rsp, 16
        mov       dword ptr [rsp], 0                ; [rsp+0] = someNotMapped
        mov       dword ptr [rsp+4], 0              ; [rsp+4] = overflow
        xor       r14, r14                          ; srcIdx
        xor       r15, r15                          ; dstPos

ALIGN 16
mainloop:
        cmp       r14d, r13d
        jae       done
        ; ---- ASCII fast path: 16 wchars all < 0x80, with room ----
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
        vpermq    ymm0, ymm0, 0D8h                  ; gather packed bytes into low 16
        vmovdqu   xmmword ptr [rbx + r15], xmm0
        add       r15, 16
        add       r14, 16
        jmp       mainloop

ALIGN 16
ascii8:
        ; ---- ASCII fast path: 8 wchars all < 0x80, with room ----
        mov       r8d, r13d
        sub       r8d, r14d
        cmp       r8d, 8
        jb        scalar_win
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
; THE ONE-OR-TWO-BYTE BLOCK: eight characters under 0x800, in about eighteen instructions.
;
; Everything under 0x800 is a lead byte and at most one continuation byte, which makes the whole
; thing a 16-bit word per character and a single VPSHUFB to drop the bytes that are not wanted.
; The general block below can do this too, and does it in forty-seven instructions; `mixed` input
; -- prose in any European or Middle Eastern script, which is ASCII spaces and punctuation
; alternating with two-byte letters -- is common enough to be worth its own path.
;
; The 0xF800 test rejects surrogates for free: every surrogate is 0xD800 or above.
; -------------------------------------------------------------------------------------------------
ALIGN 16
lat8:
        lea       r8, [r15 + 16]                    ; eight characters produce at most 16 bytes,
        cmp       r8, rdi                           ; and the store below writes all 16
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
        vmovdqu   xmmword ptr [rbx + r15], xmm2
        add       r15, rax
        add       r14, 8
        jmp       mainloop
; -------------------------------------------------------------------------------------------------
; THE BMP BLOCK: EIGHT CHARACTERS OF ANY LENGTH AT ONCE, ADDED 2026-09-16.
;
; discovery/utf8_nonascii_rows.c measured this function at 0.28x to 0.69x on every class of input
; that is not ASCII, after e71db44 had already removed the vector re-entry that was costing it a
; second time over. What was left was not a control-flow mistake but a missing kernel: the ASCII
; block above handles the one case where every character is one byte, and everything else walked one
; character at a time while ntdll converted the same text at about a cycle and a half per character.
;
; The block takes eight characters -- ANY eight characters that are not surrogates, so it covers the
; two-byte, three-byte and mixed classes together rather than one of them at a time. Each character
; is expanded into its own 32-bit lane holding all three candidate bytes plus the character itself,
; two comparisons give each character's LENGTH, PDEP packs those lengths into a two-bit-per-character
; index, and one VPSHUFB per four characters compacts the lanes into the bytes that are actually
; wanted. Nothing is branched on per character.
;
; SURROGATES ARE REJECTED WHOLESALE. A surrogate pair is four bytes from two characters and a LONE
; surrogate is three bytes of U+FFFD plus a status change; both are rare, both are irregular, and
; both are already exact in the scalar path below. A block that tried to handle them would be
; mostly tests.
;
; THE DESTINATION IS GUARDED BY 32 BYTES, not by the 24 the block can produce. Each half STORES a
; full 16 bytes and then advances by however many of them were wanted, so the second store begins at
; most 12 bytes in and ends at most 28 bytes in. Guarding by 32 is one comparison and leaves no
; arithmetic to get wrong. When the room is not there the scalar path takes over, and the scalar
; path is what defines this function's exact overflow behaviour.
; -------------------------------------------------------------------------------------------------
ALIGN 16
bmp8:
        ; THE SURROGATE TEST COMES BEFORE THE ROOM TEST, and the order is deliberate. Written the
        ; other way round -- room first -- this block's 32-byte guard would also be guarding the
        ; surrogate block below it, whose own 16-byte guard would then be unreachable: a check that
        ; can never fail, which reads like a safeguard and is not one. The mutation that halves it
        ; was the one mutant of fourteen that the correctness gate could not catch, and that was the
        ; reason. Testing the CHARACTERS first sends surrogates to their own block with its own
        ; guard still meaning something, and costs nothing: the load happens either way.
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

        lea       r10, [PACK3]                      ; the shuffle table, and its lengths --
        lea       r11, [PACK3L]                     ; two LEAs here rather than two in the
                                                    ; prologue, which measured 0.24 ns on the
                                                    ; 8-character ASCII row that never gets here
        vmovmskps r8d, ymm3
        vmovmskps r9d, ymm4
        mov       eax, 5555h
        pdep      r8d, r8d, eax                     ; one bit per character -> two bits per
        pdep      r9d, r9d, eax                     ; character, and the two sum to length-1
        add       r8d, r9d
        movzx     r9d, r8b                          ; characters 0..3
        shr       r8d, 8                            ; characters 4..7

        movzx     eax, byte ptr [r11 + r9]          ; how many bytes that half produces
        shl       r9d, 4
        vpshufb   xmm3, xmm2, xmmword ptr [r10 + r9]
        vmovdqu   xmmword ptr [rbx + r15], xmm3
        add       r15, rax

        vextracti128 xmm4, ymm2, 1
        movzx     ecx, byte ptr [r11 + r8]
        shl       r8d, 4
        vpshufb   xmm4, xmm4, xmmword ptr [r10 + r8]
        vmovdqu   xmmword ptr [rbx + r15], xmm4
        add       r15, rcx

        add       r14, 8
        jmp       mainloop

; -------------------------------------------------------------------------------------------------
; THE SURROGATE-PAIR BLOCK: FOUR PAIRS, SIXTEEN BYTES, NO SHUFFLE.
;
; The block above rejects surrogates wholesale, which left emoji and every other supplementary-plane
; character on the scalar path -- 0.92x against ntdll on 32000 characters of surrogate pairs, the one
; class still short after the BMP block landed. Pairs are worth their own block precisely BECAUSE
; they are irregular in the other block's terms and perfectly regular in their own: every valid pair
; is exactly two characters in and exactly four bytes out, so four of them are sixteen bytes with no
; length table, no PDEP and no VPSHUFB at all.
;
; The test is the whole of the validity check. Masking with 0xFC00 and comparing against the
; alternating [D800, DC00] pattern says, in one VPCMPEQW, that all eight characters are surrogates
; AND that they alternate high, low, high, low starting HERE. A lone surrogate, a low surrogate
; first, or a pair straddling the end of the eight all fail it and go to the scalar path, which is
; where this function's U+FFFD substitution and its STATUS_SOME_NOT_MAPPED already live.
;
; ALIGNMENT TAKES CARE OF ITSELF and is worth saying out loud, because it looks like it should not.
; The lanes are relative to the current index, not to the start of the string, so a run of pairs
; that begins at an ODD character offset is still high-low-high-low from the moment the index
; reaches its first high surrogate -- and the index can only arrive there after whatever preceded it
; was consumed. There is no odd-alignment case to handle.
;
; The arithmetic is VPMADDWD doing the surrogate algebra: subtracting the [D800, DC00] pattern
; leaves each half in 0..0x3FF, and multiplying by [0x400, 1] and summing adjacent pairs IS
; (hi - 0xD800) * 0x400 + (lo - 0xDC00), which is the code point less 0x10000.
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
; THE SCALAR WINDOW, ADDED 2026-09-16. Before it, every scalar character jumped back to `mainloop`
; and paid for BOTH vector blocks again -- two loads, two VPTESTs and four comparisons -- to
; discover once more that the character in front of it is not ASCII. On a string that is entirely
; two-byte characters that cost was paid on every character for the whole string, and on input that
; alternates ASCII with non-ASCII it was paid twice per character.
; discovery/utf8_nonascii_rows.c measured what it came to: the ALTERNATING class was the worst row
; in the table at 0.21x, in a change published at 2.61x on ASCII.
;
; This is change 263's rule, which this repository already wrote down and this file already broke:
; A SCALAR WALK MUST NOT RE-ENTER A VECTOR LOOP. The window is the cheapest possible statement of
; it -- once the blocks have failed, 16 source characters are encoded one at a time before they are
; tried again, so the probe is amortised over a cache line of input instead of over one character.
; The two comparisons in `scalar_next` replace two vector loads.
; -------------------------------------------------------------------------------------------------
scalar_win:
        lea       r8d, [r14 + 16]
        mov       dword ptr [rsp + 8], r8d          ; encode this far before probing again

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
        jb        low_lone
        jmp       emit3

emit1:
        lea       r8, [r15 + 1]
        cmp       r8, rdi
        jbe       e1w
        mov       dword ptr [rsp+4], 1
        jmp       done
e1w:    mov       byte ptr [rbx + r15], al
e1a:    inc       r15
        inc       r14
        jmp       scalar_next

emit2:
        lea       r8, [r15 + 2]
        cmp       r8, rdi
        jbe       e2w
        mov       dword ptr [rsp+4], 1
        jmp       done
e2w:    mov       r9d, eax
        shr       r9d, 6
        or        r9d, 0C0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
e2a:    add       r15, 2
        inc       r14
        jmp       scalar_next

emit3:
        lea       r8, [r15 + 3]
        cmp       r8, rdi
        jbe       e3w
        mov       dword ptr [rsp+4], 1
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
e3a:    add       r15, 3
        inc       r14
        jmp       scalar_next

high_surr:
        lea       r8d, [r14 + 1]
        cmp       r8d, r13d
        jae       lone
        movzx     r9d, word ptr [rsi + r8*2]        ; next
        cmp       r9d, 0DC00h
        jb        lone
        cmp       r9d, 0E000h
        jae       lone
        ; valid pair -> codepoint in eax
        sub       eax, 0D800h
        shl       eax, 10
        sub       r9d, 0DC00h
        add       eax, r9d
        add       eax, 10000h
        lea       r8, [r15 + 4]
        cmp       r8, rdi
        jbe       e4w
        mov       dword ptr [rsp+4], 1
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
e4a:    add       r15, 4
        add       r14, 2                             ; consumed 2 wchars
        jmp       scalar_next

low_lone:
lone:
        mov       dword ptr [rsp], 1                 ; someNotMapped
        lea       r8, [r15 + 3]
        cmp       r8, rdi
        jbe       ffw
        mov       dword ptr [rsp+4], 1
        jmp       done
ffw:    mov       byte ptr [rbx + r15], 0EFh
        mov       byte ptr [rbx + r15 + 1], 0BFh
        mov       byte ptr [rbx + r15 + 2], 0BDh
ffa:    add       r15, 3
        inc       r14
        jmp       scalar_next

scalar_next:
        cmp       r14d, r13d
        jae       done                              ; the source is finished
        cmp       r14d, dword ptr [rsp + 8]
        jb        scalar_char                       ; still inside the window: stay scalar
        jmp       mainloop                          ; the window is spent: probe the blocks again

done:
        mov       dword ptr [r12], r15d             ; *outLen = dstPos
        mov       eax, dword ptr [rsp+4]
        test      eax, eax
        jnz       ret_small
        mov       eax, dword ptr [rsp]
        test      eax, eax
        jnz       ret_nm
        xor       eax, eax
        jmp       epi
ret_small:
        mov       eax, 0C0000023h
        jmp       epi
ret_nm:
        mov       eax, 107h
epi:
        add       rsp, 16
        vzeroupper
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
; ---------------------------------------------------------------------------------------------
; THE MEASURING MODE. Entered before the prologue, so it is a leaf: no pushes, nothing but the
; volatile registers and the caller's shadow space, which holds the one flag it needs.
;
;   r8 = &produced,  r9 = src,  [rsp+40] = srcBytes  (the fifth argument, with no pushes yet)
;
; One character at a time, because this path is not the performance case -- the conversion is.
; The rule: below 0x80 is one byte, below 0x800 is two, a HIGH surrogate followed by a LOW one is
; four and consumes both, and everything else -- including a lone surrogate, which becomes U+FFFD
; -- is three. A lone surrogate also makes the status STATUS_SOME_NOT_MAPPED, exactly as the
; conversion path reports it.
; ---------------------------------------------------------------------------------------------
u2u8_measure:
        mov       dword ptr [rsp + 8], 0            ; someNotMapped, in the caller's shadow space
        mov       r10d, dword ptr [rsp + 40]        ; srcBytes
        shr       r10d, 1                           ; ... as a count of wchars
        xor       r11d, r11d                        ; bytes so far
        xor       ecx, ecx                          ; index
        jmp       m_test

; THE ASCII BLOCK, ADDED 2026-09-16 BECAUSE A CALLER MEASURED IT. The first version of this mode
; walked one character at a time, which is the right shape for a path nothing calls in a loop --
; and then change 268 called it on every allocating conversion, where the shipped code's own sizing
; pass is vectorised. On 4000 ASCII characters that scalar walk cost more than the conversion it
; was sizing, and the allocating row of change 268's bench came out at 0.47x. Sixteen characters
; are tested in one VPTEST here: if none of them has a bit above 0x7F, all sixteen are one byte
; each and the count moves by 16 with no per-character work at all. Anything else falls into the
; scalar rule below, which is unchanged and still decides every non-ASCII case.
ALIGN 16
m_fast: vmovdqu   ymm0, ymmword ptr [r9 + rcx*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80y]
        vptest    ymm1, ymm1
        jnz       m_loop                            ; not all ASCII: one character at a time
        add       r11d, 16
        add       ecx, 16
        jmp       m_test
ALIGN 16
m_loop:
        movzx     eax, word ptr [r9 + rcx*2]
        cmp       eax, 80h
        jb        m_one
        cmp       eax, 800h
        jb        m_two
        mov       edx, eax
        and       edx, 0F800h
        cmp       edx, 0D800h
        jne       m_three                           ; not a surrogate at all
        cmp       eax, 0DC00h
        jae       m_lone                            ; a LOW surrogate first is always lone
        lea       edx, [rcx + 1]
        cmp       edx, r10d
        jae       m_lone                            ; nothing follows it
        movzx     edx, word ptr [r9 + rcx*2 + 2]
        sub       edx, 0DC00h
        cmp       edx, 400h
        jae       m_lone                            ; what follows is not a low surrogate
        add       r11d, 4                           ; a valid pair: four bytes, two units consumed
        add       ecx, 2
        jmp       m_test
m_lone: mov       dword ptr [rsp + 8], 1
m_three:add       r11d, 3
        inc       ecx
        jmp       m_test
m_two:  add       r11d, 2
        inc       ecx
        jmp       m_test
m_one:  inc       r11d
        inc       ecx
m_test: mov       edx, r10d
        sub       edx, ecx
        cmp       edx, 16
        jae       m_fast                            ; sixteen left: try them as a block
        cmp       ecx, r10d
        jb        m_loop
        mov       dword ptr [r8], r11d
        xor       eax, eax
        cmp       dword ptr [rsp + 8], 0
        je        m_ret
        mov       eax, 107h                         ; STATUS_SOME_NOT_MAPPED
m_ret:  vzeroupper
        ret

wia_u2u8 ENDP
END
