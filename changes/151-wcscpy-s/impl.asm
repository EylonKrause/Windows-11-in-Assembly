; changes/151-wcscpy-s/impl.asm
; errno_t wia_wcscpy_s(wchar_t* dst, rsize_t size, const wchar_t* src)   [Win64: rcx, rdx, r8]
;
; The wide twin of change 150. ucrtbase!wcscpy_s is the same UCRT scalar loop, one wchar per
; iteration, 62.5 ns for 254 wide characters, ~1.1 cycles per character.
;
; Same contract as strcpy_s with `size` counted in wchar_t:
;   - dst == NULL or size == 0 -> handler, EINVAL (22), dst untouched;
;   - src == NULL              -> dst[0] = 0, handler, EINVAL;
;   - src fits                 -> exactly len+1 wchars written, return 0;
;   - src does not fit         -> exactly `size` wchars of src written FIRST, then dst[0] = 0, then
;                                 the handler, then ERANGE (34).
; The handler is reached through ucrtbase's own exported `_invalid_parameter_noinfo`, so the error
; paths (including the default handler's __fastfail) are indistinguishable from the live ones.
;
; Everything below works in BYTES: `size` is doubled up front, with a saturating
; `shl / sbb / or` so a nonsensical size near 2^63 clamps instead of wrapping to a small bound.
; From there this is a byte-for-byte port of 150 with `vpcmpeqb` -> `vpcmpeqw`. `vpcmpeqw` sets both
; bytes of a matching word, so `tzcnt` lands on the low (even) byte of the terminator, no rounding
; is needed here, unlike the `bsr` in change 132/149.
;
; Reads are 32-byte ALIGNED loads (align down, shift the leading bytes out of the mask); a wchar can
; never straddle a block because both the block base and src are even. The scan stops after the block
; holding the last in-bounds byte, so the copy that follows only re-reads memory the scan already
; touched. Writes are exactly n bytes: the head/tail pair overlaps INSIDE [0, n).
;
; ISA: AVX2. Validated on Zen3.

EXTERN _invalid_parameter_noinfo:PROC

; --- copy exactly r10 bytes from r8 to r11 (r10 >= 2, always even); clobbers rax, rcx, r9, ymm0 ---
COPYN MACRO
    LOCAL c16, c8, c4, c2, cbig, cloop, clast, cdone
        cmp       r10, 32
        jae       cbig
        cmp       r10, 16
        jae       c16
        cmp       r10, 8
        jae       c8
        cmp       r10, 4
        jae       c4
c2:     movzx     eax, word ptr [r8]
        mov       word ptr [r11], ax
        jmp       cdone
c4:     mov       eax, dword ptr [r8]
        mov       dword ptr [r11], eax
        mov       ecx, dword ptr [r8 + r10 - 4]
        mov       dword ptr [r11 + r10 - 4], ecx
        jmp       cdone
c8:     mov       rax, qword ptr [r8]
        mov       qword ptr [r11], rax
        mov       rcx, qword ptr [r8 + r10 - 8]
        mov       qword ptr [r11 + r10 - 8], rcx
        jmp       cdone
c16:    vmovdqu   xmm0, xmmword ptr [r8]
        vmovdqu   xmmword ptr [r11], xmm0
        vmovdqu   xmm0, xmmword ptr [r8 + r10 - 16]
        vmovdqu   xmmword ptr [r11 + r10 - 16], xmm0
        jmp       cdone
cbig:   mov       rcx, r8
        mov       rax, r11
        mov       r9, r10
cloop:  cmp       r9, 32
        jbe       clast
        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymmword ptr [rax], ymm0
        add       rcx, 32
        add       rax, 32
        sub       r9, 32
        jmp       cloop
clast:  vmovdqu   ymm0, ymmword ptr [r8 + r10 - 32]   ; final 32, overlapping backwards inside [0,n)
        vmovdqu   ymmword ptr [r11 + r10 - 32], ymm0
cdone:
ENDM

.code
wia_wcscpy_s PROC
        test      rcx, rcx
        jz        ws_einval_nowrite
        test      rdx, rdx
        jz        ws_einval_nowrite
        mov       r11, rcx                          ; r11 = dst  (rcx is needed for the shift count)
        shl       rdx, 1                            ; size: wchars -> bytes ...
        sbb       rax, rax                          ; ... CF set only for an absurd size >= 2^63,
        or        rdx, rax                          ;     which saturates instead of wrapping small
        test      r8, r8
        jz        ws_src_null

        ; ---- bounded terminator scan, page-safe ------------------------------------------------
        vpxor     ymm1, ymm1, ymm1
        mov       r9, r8
        and       r9, -32                           ; align the first load down
        mov       ecx, r8d
        and       ecx, 31                           ; bytes of that block that precede src (even)
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shr       eax, cl                           ; bit i now means byte i of src is in a zero word
        mov       r10d, 32
        sub       r10d, ecx                         ; bytes of src covered by the prologue block
        test      eax, eax
        jnz       ws_found_lo

ws_next:
        cmp       r10, rdx
        jae       ws_erange                         ; covered every in-bounds byte, no terminator
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       ws_found_hi
        add       r10, 32
        jmp       ws_next

ws_found_hi:
        tzcnt     eax, eax                          ; low byte of the terminating word -> even
        add       rax, r10
        jmp       ws_have
ws_found_lo:
        tzcnt     eax, eax
ws_have:
        cmp       rax, rdx
        jae       ws_erange                         ; the terminator sits at or past `size` wchars
        lea       r10, [rax + 2]                    ; copy the terminator too
        COPYN
        xor       eax, eax
        vzeroupper
        ret

        ; ---- src does not fit: copy exactly `size` wchars, empty dst, then ERANGE ---------------
ws_erange:
        mov       r10, rdx
        COPYN
        mov       word ptr [r11], 0
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

ws_src_null:
        mov       word ptr [r11], 0
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

ws_einval_nowrite:
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret
wia_wcscpy_s ENDP
END
