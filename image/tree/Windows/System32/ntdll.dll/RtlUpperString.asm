; ntdll.dll!RtlUpperString  --  hand-written x86-64 reimplementation (88.5x vs shipped)
; source of truth: changes/165-rtlupperstring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/165-rtlupperstring/impl.asm
; void wia_rtlupperstring(STRING* Destination, const STRING* Source)   [Win64: rcx, rdx]
;
; Reimplements ntdll!RtlUpperString: copy Source into Destination, uppercasing as it goes.
;
; The live one takes 1485 ns for 254 characters, 5.85 ns PER CHARACTER. That is not a slow loop,
; it is a CALL per character: ntdll!RtlUpperChar measures 5.47 ns on its own, and RtlUpperString
; agrees with it byte for byte over all 256 inputs. So the routine is paying full call overhead to
; translate one byte at a time.
;
; ---- the mapping is plain ASCII, and that was verified rather than assumed --------------------
; A per-character mapping table is normally where a reimplementation like this dies, because the
; table is locale-dependent and cannot be baked in honestly (see the case-folding scope-out for
; PathCommonPrefixW). Not here: RtlUpperChar was called for ALL 256 byte values and compared against
; the plain rule
;       c in 'a'..'z'  ->  c - 32,   everything else unchanged
; with **zero** deviations. The high half 0x80..0xFF is left completely untouched. So there is no
; table to reproduce and nothing locale-dependent to get wrong.
;
; Contract (probed against the live export):
;   n = min(Source->Length, Destination->MaximumLength); both are BYTE counts
;   Destination->Buffer[0..n) = uppercased Source->Buffer[0..n)
;   Destination->Length = n
;   Destination->MaximumLength is not touched, and NO terminator is written, the byte just past
;   the result keeps its previous value. Truncation when the destination is smaller is silent: a
;   MaximumLength of 3 against a 100-byte source yields Length = 3 and no error of any kind.
;
; ---- method ------------------------------------------------------------------------------------
; The uppercase test is the usual unsigned-range trick, which needs no table at all. AVX2 has no
; unsigned byte compare, so the range is biased into signed territory:
;       t = c + 0x1F              (that is (c - 'a') XOR 0x80, since XOR 0x80 == +0x80 mod 256)
;       lowercase  <=>  t <= -103 signed   <=>  vpcmpgtb(-102, t)
; and the mask then selects a 0x20 to subtract. Four instructions per 32 bytes, no lookup.
;
; The copy is strictly forward, 32-byte blocks then a descending 16/8/4/2/1 ladder, each step
; loading before it stores. The head-plus-overlapping-tail trick used elsewhere in this repository
; is deliberately NOT used: the live routine is a forward per-character copy, so on overlapping
; buffers a forward copy is what reproduces it, and an overlapping tail store could differ.
;
; ISA: AVX2. Validated on Zen3.

.const
ALIGN 16
c_bias  db 01Fh                               ; +0x1F maps 'a'..'z' onto the low signed range
c_lim   db 09Ah                               ; -102 signed
c_case  db 020h                               ; the bit that separates the cases

.code
wia_rtlupperstring PROC
        movzx     eax, word ptr [rdx]                ; Source->Length
        movzx     r8d, word ptr [rcx + 2]            ; Destination->MaximumLength
        cmp       eax, r8d
        cmova     eax, r8d
        mov       word ptr [rcx], ax                 ; Destination->Length = n
        mov       r9, qword ptr [rdx + 8]            ; Source->Buffer
        mov       r10, qword ptr [rcx + 8]           ; Destination->Buffer
        mov       r11d, eax                          ; n, in bytes
        test      r11d, r11d
        jz        us_done

        vpbroadcastb ymm3, byte ptr c_bias    ; MASM rejects ALIGN 32 in .const, so broadcast
        vpbroadcastb ymm4, byte ptr c_lim     ;   from single bytes instead of loading vectors
        vpbroadcastb ymm5, byte ptr c_case

us_32:  cmp       r11d, 32
        jb        us_16
        vmovdqu   ymm0, ymmword ptr [r9]
        vpaddb    ymm1, ymm0, ymm3
        vpcmpgtb  ymm1, ymm4, ymm1                   ; -102 > t  <=>  the byte is lowercase
        vpand     ymm1, ymm1, ymm5
        vpsubb    ymm0, ymm0, ymm1
        vmovdqu   ymmword ptr [r10], ymm0
        add       r9, 32
        add       r10, 32
        sub       r11d, 32
        jmp       us_32

us_16:  cmp       r11d, 16
        jb        us_8
        vmovdqu   xmm0, xmmword ptr [r9]
        vpaddb    xmm1, xmm0, xmm3
        vpcmpgtb  xmm1, xmm4, xmm1
        vpand     xmm1, xmm1, xmm5
        vpsubb    xmm0, xmm0, xmm1
        vmovdqu   xmmword ptr [r10], xmm0
        add       r9, 16
        add       r10, 16
        sub       r11d, 16

us_8:   cmp       r11d, 8
        jb        us_4
        vmovq     xmm0, qword ptr [r9]
        vpaddb    xmm1, xmm0, xmm3
        vpcmpgtb  xmm1, xmm4, xmm1
        vpand     xmm1, xmm1, xmm5
        vpsubb    xmm0, xmm0, xmm1
        vmovq     qword ptr [r10], xmm0
        add       r9, 8
        add       r10, 8
        sub       r11d, 8

us_4:   cmp       r11d, 4
        jb        us_1
        vmovd     xmm0, dword ptr [r9]
        vpaddb    xmm1, xmm0, xmm3
        vpcmpgtb  xmm1, xmm4, xmm1
        vpand     xmm1, xmm1, xmm5
        vpsubb    xmm0, xmm0, xmm1
        vmovd     dword ptr [r10], xmm0
        add       r9, 4
        add       r10, 4
        sub       r11d, 4

us_1:   test      r11d, r11d
        jz        us_ret
us_1l:  movzx     eax, byte ptr [r9]
        mov       r8d, eax
        sub       r8d, 'a'
        cmp       r8d, 25
        ja        us_1s
        sub       eax, 32
us_1s:  mov       byte ptr [r10], al
        inc       r9
        inc       r10
        dec       r11d
        jnz       us_1l

us_ret:
        vzeroupper
us_done:
        ret
wia_rtlupperstring ENDP
END
