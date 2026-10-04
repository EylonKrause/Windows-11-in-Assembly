; shlwapi.dll!StrCmpICA  --  hand-written x86-64 reimplementation (3.05x vs shipped)
; source of truth: changes/305-strcmpca/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/305-strcmpca/impl.asm
;   int wia_strcmpca  (PCSTR a, PCSTR b)             [Win64: rcx, rdx -> eax]
;   int wia_strcmpica (PCSTR a, PCSTR b)
;   int wia_strcmpnca (PCSTR a, PCSTR b, int n)      [r8d = n]
;   int wia_strcmpnica(PCSTR a, PCSTR b, int n)
;
; shlwapi's "C collation" comparisons for 8-bit strings, StrCmpCA / StrCmpICA / StrCmpNCA / StrCmpNICA:
; the byte siblings of change 304. discovery/shlwapi_str_c.c timed StrCmpCA at 0.45 ns per byte on
; equal strings, 6.3x the C runtime's strcmp; kernelbase's body is a one-byte loop. What they compute,
; from discovery/strcmpc_contract.c, exhaustively over all byte pairs:
;
;   * StrCmpCA / StrCmpNCA: ordinal on UNSIGNED bytes; the value is the difference a[i] - b[i];
;   * StrCmpICA / StrCmpNICA: 'A'..'Z' fold to 'a'..'z' and nothing else, compared as SIGNED chars --
;     the value is the difference of the folded, sign-extended bytes ("\xC0" vs "a" is negative here
;     and positive in StrCmpCA). Both models were checked to the exact value, 0 disagreements;
;   * NULL faults; the N forms are 304's: n == 0 reads nothing, n is an unsigned 32-bit count (a negative
;     int is unbounded), index n is never read.
;
; Signedness only reaches the RETURN value: a stop is "the bytes differ, or a ended", which does not
; depend on it, so the vector stages are the same for all four and only the scalar re-read at the stop
; is movzx or movsx.
;
; Same three stages as 304, at byte width: an unrolled scalar head (bytes 0..5 unbounded, 0..3 counted);
; one 16-byte xmm block right after it behind a plain page test, with no vzeroupper because nothing there
; is 256-bit; then a
; ymm loop of 32 bytes per block, loading only while BOTH windows lie inside the pages their cursors are
; in, and stepping one byte at a time within 32 bytes of either page end, as the export does. So it
; faults exactly where the export faults: when the strings are equal up to an unreadable page.
;
; Why six head bytes here and four in 304: kernelbase's byte loop costs about a cycle a byte for the first
; few, as cheap as this head, so the xmm block's ~3-cycle entry only pays once the export's per-byte cost
; has grown. Measured: with a 4-byte head the 4-byte string was 0.95x the export; with 6, 1.13x.
;
; Stop mask: vpminub(vpcmpeqb(a, b), a) == 0, zero exactly where the bytes differ or a ends. Fold: (c +
; 0x3F) as a signed byte is at most 0x99 exactly for 'A'..'Z', so an add, a signed compare and an andn
; with 0x20 fold 32 bytes.
;
; Registers: rax, rcx, rdx, r8-r11 (r11 = FTAB in the I forms), ymm0-ymm5. No prologue, no unwind data.  ISA: AVX2 + BMI1.

OPTION PROC:PRIVATE
PUBLIC wia_strcmpca
PUBLIC wia_strcmpica
PUBLIC wia_strcmpnca
PUBLIC wia_strcmpnica

.const
ALIGN 16
KBIAS   DB      32 dup(3Fh)                     ; 'A' + 3Fh = 80h
KTHR    DB      32 dup(99h)                     ; 'Z' + 3Fh
K20     DB      32 dup(20h)                     ; the case distance
; FTAB[b] = the folded byte as a SIGNED char: 'A'..'Z' -> 'a'..'z', everything else itself. movsx from
; here is the whole scalar fold of the I forms -- two instructions a byte where the arithmetic fold
; took five, which on "differ at 0" was the difference between 0.89x and a win.
ALIGN 16
FTAB LABEL BYTE
        DB      000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 008h, 009h, 00Ah, 00Bh, 00Ch, 00Dh, 00Eh, 00Fh
        DB      010h, 011h, 012h, 013h, 014h, 015h, 016h, 017h, 018h, 019h, 01Ah, 01Bh, 01Ch, 01Dh, 01Eh, 01Fh
        DB      020h, 021h, 022h, 023h, 024h, 025h, 026h, 027h, 028h, 029h, 02Ah, 02Bh, 02Ch, 02Dh, 02Eh, 02Fh
        DB      030h, 031h, 032h, 033h, 034h, 035h, 036h, 037h, 038h, 039h, 03Ah, 03Bh, 03Ch, 03Dh, 03Eh, 03Fh
        DB      040h, 061h, 062h, 063h, 064h, 065h, 066h, 067h, 068h, 069h, 06Ah, 06Bh, 06Ch, 06Dh, 06Eh, 06Fh
        DB      070h, 071h, 072h, 073h, 074h, 075h, 076h, 077h, 078h, 079h, 07Ah, 05Bh, 05Ch, 05Dh, 05Eh, 05Fh
        DB      060h, 061h, 062h, 063h, 064h, 065h, 066h, 067h, 068h, 069h, 06Ah, 06Bh, 06Ch, 06Dh, 06Eh, 06Fh
        DB      070h, 071h, 072h, 073h, 074h, 075h, 076h, 077h, 078h, 079h, 07Ah, 07Bh, 07Ch, 07Dh, 07Eh, 07Fh
        DB      080h, 081h, 082h, 083h, 084h, 085h, 086h, 087h, 088h, 089h, 08Ah, 08Bh, 08Ch, 08Dh, 08Eh, 08Fh
        DB      090h, 091h, 092h, 093h, 094h, 095h, 096h, 097h, 098h, 099h, 09Ah, 09Bh, 09Ch, 09Dh, 09Eh, 09Fh
        DB      0A0h, 0A1h, 0A2h, 0A3h, 0A4h, 0A5h, 0A6h, 0A7h, 0A8h, 0A9h, 0AAh, 0ABh, 0ACh, 0ADh, 0AEh, 0AFh
        DB      0B0h, 0B1h, 0B2h, 0B3h, 0B4h, 0B5h, 0B6h, 0B7h, 0B8h, 0B9h, 0BAh, 0BBh, 0BCh, 0BDh, 0BEh, 0BFh
        DB      0C0h, 0C1h, 0C2h, 0C3h, 0C4h, 0C5h, 0C6h, 0C7h, 0C8h, 0C9h, 0CAh, 0CBh, 0CCh, 0CDh, 0CEh, 0CFh
        DB      0D0h, 0D1h, 0D2h, 0D3h, 0D4h, 0D5h, 0D6h, 0D7h, 0D8h, 0D9h, 0DAh, 0DBh, 0DCh, 0DDh, 0DEh, 0DFh
        DB      0E0h, 0E1h, 0E2h, 0E3h, 0E4h, 0E5h, 0E6h, 0E7h, 0E8h, 0E9h, 0EAh, 0EBh, 0ECh, 0EDh, 0EEh, 0EFh
        DB      0F0h, 0F1h, 0F2h, 0F3h, 0F4h, 0F5h, 0F6h, 0F7h, 0F8h, 0F9h, 0FAh, 0FBh, 0FCh, 0FDh, 0FEh, 0FFh

; A segment of its own, 64-byte aligned: with .code's 16-byte alignment the same loop bytes landed at
; different offsets in each build and the 256-byte row moved between 298 and 454 ns. Aligning the loop
; narrows that but does not remove it (363 and 433 ns in the two aligned builds measured).
WIATEXT SEGMENT ALIGN(64) 'CODE'

IFNDEF HLEN
HLEN    EQU     6                               ; scalar head bytes of the unbounded forms
ENDIF

; eax <- byte at [a], r9d <- byte at [b]: zero-extended, or for the folding forms the folded byte
; sign-extended, through FTAB (r11).
LD2 MACRO fold, a, b
IF fold
        movzx     eax, byte ptr a
        movzx     r9d, byte ptr b
        movsx     eax, byte ptr [r11 + rax]
        movsx     r9d, byte ptr [r11 + r9]
ELSE
        movzx     eax, byte ptr a
        movzx     r9d, byte ptr b
ENDIF
ENDM

; ymm r <- fold(r), using ymm5. ymm3 = KBIAS, ymm4 = KTHR.
VFOLD MACRO r
        vpaddb    ymm5, r, ymm3
        vpcmpgtb  ymm5, ymm5, ymm4              ; NOT an upper-case letter
        vpandn    ymm5, ymm5, ymmword ptr [K20]
        vpaddb    r, r, ymm5
ENDM

; xmm r <- fold(r), constants from memory.
XFOLD MACRO r
        vpaddb    xmm5, r, xmmword ptr [KBIAS]
        vpcmpgtb  xmm5, xmm5, xmmword ptr [KTHR]
        vpandn    xmm5, xmm5, xmmword ptr [K20]
        vpaddb    r, r, xmm5
ENDM

; one head byte at offset off: continuing is the TAKEN branch, stopping an inline ret (304's layout,
; which costs an empty or one-byte string no branch at all).
HCHAR MACRO off, fold
        LOCAL r_k, n_k
        LD2       fold, [rcx + off], [rdx + off]
        sub       eax, r9d
        jnz       r_k
        test      r9d, r9d
        jnz       n_k
r_k:
        ret
n_k:
ENDM

; one byte of a bounded body: continuing falls through, a stop jumps to lstop (a ret).
NCHAR MACRO off, fold, lstop
        LD2       fold, [rcx + off], [rdx + off]
        sub       eax, r9d
        jnz       lstop
        test      r9d, r9d
        jz        lstop
ENDM

; the n-th byte: its (folded) difference is the answer whatever it is.
LCHAR MACRO off, fold
        LD2       fold, [rcx + off], [rdx + off]
        sub       eax, r9d
        ret
ENDM

STRCMPA MACRO name, fold, bounded
        LOCAL l_page, l_block, l_scalar, l_found, l_vret, l_zero, l_zero0, l_go, l_big, l_n2, l_n3, l_stop, l_four, l_xs, l_stop2
ALIGN 64
name PROC
IF fold
        lea       r11, FTAB                     ; the scalar fold table, for every scalar re-read
ENDIF
IF bounded
        ; --- n < 4 is dispatched once, to a body that never tests the count ---
        mov       r8d, r8d
        cmp       r8, 4
        jae       l_big
        cmp       r8d, 2
        ja        l_n3
        je        l_n2
        test      r8d, r8d
        jz        l_zero0                       ; n == 0: 0, nothing read
        LCHAR     0, fold                       ; n == 1
l_n2:
        NCHAR     0, fold, l_stop
        LCHAR     1, fold
l_n3:
        NCHAR     0, fold, l_stop
        NCHAR     1, fold, l_stop
        LCHAR     2, fold
l_stop:
        ret
l_zero0:
        xor       eax, eax
        ret
l_big:
ENDIF
        ; --- bytes 0..3, scalar and unrolled; with n >= 4 none is past the count, and with n == 4 the
        ;     fourth is the n-th ---
IF bounded
        NCHAR     0, fold, l_stop2
        NCHAR     1, fold, l_stop2
        NCHAR     2, fold, l_stop2
        cmp       r8, 4
        je        l_four
        NCHAR     3, fold, l_stop2
ELSE
        hc = 0
        REPT HLEN
        HCHAR     %hc, fold
        hc = hc + 1
        ENDM
ENDIF
IF bounded
        xo = 4
ELSE
        xo = HLEN
ENDIF
        ; --- bytes 4..19 as one xmm block when both 16-byte windows lie inside their pages ---
        lea       r10d, [rcx + xo]
        and       r10d, 4095
        cmp       r10d, 4080
        ja        l_go
        lea       r10d, [rdx + xo]
        and       r10d, 4095
        cmp       r10d, 4080
        ja        l_go
        vmovdqu   xmm1, xmmword ptr [rcx + xo]
IF fold
        vmovdqu   xmm2, xmmword ptr [rdx + xo]
        XFOLD     xmm1
        XFOLD     xmm2
        vpcmpeqb  xmm2, xmm2, xmm1
ELSE
        vpcmpeqb  xmm2, xmm1, xmmword ptr [rdx + xo]
ENDIF
        vpminub   xmm2, xmm2, xmm1
        vpxor     xmm0, xmm0, xmm0
        vpcmpeqb  xmm2, xmm2, xmm0
        vpmovmskb r9d, xmm2
        tzcnt     r9d, r9d                      ; CF: no stop among bytes 4..19
IF bounded
        jnc       l_xs
        cmp       r8, 20
        ja        l_go
        xor       eax, eax                      ; all n bytes equal
        ret
l_xs:
        lea       r10d, [r9 + 4]
        cmp       r10, r8
        jae       l_zero0                       ; the stop lies at or past index n
ELSE
        jc        l_go
ENDIF
        LD2       fold, [rcx + r9 + xo], [rdx + r9 + xo]
        sub       eax, r9d
        ret
IF bounded
l_four:
        LCHAR     3, fold
l_stop2:
        ret
ENDIF
l_go:
        sub       rdx, rcx                      ; b, as an offset from a's cursor
        vpxor     xmm0, xmm0, xmm0
IF fold
        vmovdqu   ymm3, ymmword ptr [KBIAS]
        vmovdqu   ymm4, ymmword ptr [KTHR]
ENDIF
l_page:
        mov       r9d, ecx
        and       r9d, 4095
        lea       r10, [rcx + rdx]
        and       r10d, 4095
        cmp       r9d, r10d
        cmovb     r9d, r10d                     ; the cursor nearer its page end
        cmp       r9d, 4064
        ja        l_scalar                      ; a 32-byte window would cross it
        mov       r10d, 4064
        sub       r10d, r9d
        shr       r10d, 5                       ; whole blocks AFTER this one inside both pages
ALIGN 64
l_block:
        vmovdqu   ymm1, ymmword ptr [rcx]
IF fold
        vmovdqu   ymm2, ymmword ptr [rcx + rdx]
        VFOLD     ymm1
        VFOLD     ymm2
        vpcmpeqb  ymm2, ymm2, ymm1
ELSE
        vpcmpeqb  ymm2, ymm1, ymmword ptr [rcx + rdx]
ENDIF
        vpminub   ymm2, ymm2, ymm1              ; a where equal, 0 where they differ ...
        vpcmpeqb  ymm2, ymm2, ymm0              ; ... so 0 marks a difference or a's end
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       l_found
IF bounded
        sub       r8, 32
        jbe       l_zero                        ; the n bytes are all equal
ENDIF
        add       rcx, 32
        sub       r10d, 1
        jae       l_block
        jmp       l_page
l_scalar:
        LD2       fold, [rcx], [rcx + rdx]
        sub       eax, r9d
        jnz       l_vret
        test      r9d, r9d
        jz        l_vret
        add       rcx, 1
IF bounded
        sub       r8, 1
        jz        l_vret                        ; eax = 0
ENDIF
        jmp       l_page
l_found:
        tzcnt     r9d, r9d                      ; index of the first stop in the block
IF bounded
        cmp       r9, r8
        jae       l_zero                        ; it lies at or past index n
ENDIF
        add       rcx, r9
        LD2       fold, [rcx], [rcx + rdx]
        sub       eax, r9d
l_vret:
        vzeroupper
        ret
l_zero:
        xor       eax, eax
        vzeroupper
        ret
name ENDP
ENDM

STRCMPA wia_strcmpca,   0, 0
STRCMPA wia_strcmpica,  1, 0
STRCMPA wia_strcmpnca,  0, 1
STRCMPA wia_strcmpnica, 1, 1

WIATEXT ENDS
END
