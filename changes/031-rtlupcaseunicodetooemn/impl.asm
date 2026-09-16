; changes/031-rtlupcaseunicodetooemn/impl.asm
; NTSTATUS wia_u2uoem(char* dst, ULONG maxBytes, PULONG outLen, const wchar_t* src, ULONG srcBytes)
;   [Win64: rcx, edx, r8, r9, [rsp+0x28] -> eax; *outLen = bytes written]
;
; Reimplements ntdll!RtlUpcaseUnicodeToOemN (upcase then narrow UTF-16 -> single-byte
; ANSI codepage, 1 byte per wchar). Writes up to maxBytes; *outLen = bytes written. All-ASCII
; 16-wchar blocks pack 16 -> 16 bytes (vpackuswb + vpermq); other wchars via the
; 65536-entry wia_ansimap[] byte table from the OS codepage.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_upoemmap:BYTE

.const
ALIGN 16
CFF80   DW      16 dup(0FF80h)
C0060   DW      16 dup(0060h)
C007A   DW      16 dup(007Ah)
C0020   DW      16 dup(0020h)

.code
wia_u2uoem PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        mov       rbx, rcx                          ; dst
        mov       edi, edx                          ; maxBytes
        mov       r12, r8                           ; outLen ptr
        mov       rsi, r9                           ; src
        mov       r13d, dword ptr [rsp + 50h]       ; srcBytes
        shr       r13d, 1                           ; n wchars
        xor       r9d, r9d                          ; i (== bytes written)
        lea       rcx, wia_upoemmap
mb_loop:
        cmp       r9d, r13d
        jae       mb_done
        cmp       r9d, edi
        jae       mb_done                           ; output full
        ; ASCII fast path: 16 wchars, room for 16 bytes, all < 0x80
        mov       r8d, r13d
        sub       r8d, r9d
        cmp       r8d, 16
        jb        mb_ascii8
        mov       r8d, edi
        sub       r8d, r9d
        cmp       r8d, 16
        jb        mb_ascii8
        vmovdqu   ymm0, ymmword ptr [rsi + r9*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80]
        vptest    ymm1, ymm1
        jnz       mb_ascii8
        vpcmpgtw  ymm2, ymm0, ymmword ptr [C0060]
        vpcmpgtw  ymm3, ymm0, ymmword ptr [C007A]
        vpandn    ymm2, ymm3, ymm2
        vpand     ymm2, ymm2, ymmword ptr [C0020]
        vpsubw    ymm0, ymm0, ymm2
        vpackuswb ymm0, ymm0, ymm0
        vpermq    ymm0, ymm0, 0D8h
        vmovdqu   xmmword ptr [rbx + r9], xmm0
        add       r9, 16
        jmp       mb_loop
mb_ascii8:
        mov       r8d, r13d
        sub       r8d, r9d
        cmp       r8d, 8
        jb        mb_scalar
        mov       r8d, edi
        sub       r8d, r9d
        cmp       r8d, 8
        jb        mb_scalar
        vmovdqu   xmm0, xmmword ptr [rsi + r9*2]
        vpand     xmm1, xmm0, xmmword ptr [CFF80]
        vptest    xmm1, xmm1
        jnz       mb_scalar
        vpcmpgtw  xmm2, xmm0, xmmword ptr [C0060]
        vpcmpgtw  xmm3, xmm0, xmmword ptr [C007A]
        vpandn    xmm2, xmm3, xmm2
        vpand     xmm2, xmm2, xmmword ptr [C0020]
        vpsubw    xmm0, xmm0, xmm2
        vpackuswb xmm0, xmm0, xmm0
        vmovq     qword ptr [rbx + r9], xmm0
        add       r9, 8
        jmp       mb_loop
; -------------------------------------------------------------------------------------------------
; THE TABLE PATH TAKES A RUN, ADDED 2026-09-16. It used to convert ONE character and jump back to
; `mb_loop`, which re-ran the 16-wide test AND the 8-wide test -- two vector loads, two VPTESTs and
; four bound comparisons -- to discover once more that the character in front of it is not ASCII.
; On text where the vector block never applies, that cost was paid on every character for the whole
; string.
;
; discovery/upcase_nonascii_rows.c measured what it came to: 0.59x against the shipped export on
; Cyrillic or CJK, in a change published at 10.24x on ASCII. And the sibling that does the same job
; with the same table -- change 020, RtlUpcaseUnicodeStringToAnsiString -- already converts SIXTEEN
; characters per visit and measures 2.62x on the same text. This is change 263's rule, which this
; repository wrote down and this file broke:
;
;       A SCALAR WALK MUST NOT RE-ENTER A VECTOR LOOP.
;
; The run is bounded by BOTH limits this function has -- characters remaining and output room
; remaining -- so the loop below cannot overrun either, and `mb_loop` still owns the decision to
; stop. Sixteen is the vector block's own width: long enough to amortise the probe, short enough
; that text alternating ASCII with anything else still reaches the vector path.
; -------------------------------------------------------------------------------------------------
mb_scalar:
        mov       r8d, r13d
        sub       r8d, r9d                          ; characters left
        mov       edx, edi
        sub       edx, r9d                          ; bytes of room left
        cmp       r8d, edx
        cmova     r8d, edx                          ; whichever runs out first
        cmp       r8d, 16
        jbe       mb_sb
        mov       r8d, 16
mb_sb:  movzx     eax, word ptr [rsi + r9*2]
        movzx     eax, byte ptr [rcx + rax]
        mov       byte ptr [rbx + r9], al
        add       r9, 1
        dec       r8d
        jnz       mb_sb
        jmp       mb_loop
mb_done:
        mov       dword ptr [r12], r9d              ; *outLen = bytes written
        xor       eax, eax
        cmp       r9d, r13d
        jae       mb_ret
        mov       eax, 80000005h                    ; STATUS_BUFFER_OVERFLOW (this fn reports it)
mb_ret:
        vzeroupper
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_u2uoem ENDP
END
