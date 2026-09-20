; changes/155-wcsncpy-s/impl.asm
; errno_t wia_wcsncpy_s(wchar_t* dst, rsize_t size, const wchar_t* src, rsize_t count)
;   [Win64: rcx, rdx, r8, r9]
;
; The wide twin of change 154, with `size` and `count` in wchar_t. ucrtbase!wcsncpy_s is the same
; UCRT two-counter scalar loop, one wide character per iteration, 63 ns for 254 wide characters.
;
; Contract, identical to strncpy_s in wide units, including all three of the traps 154 documents:
;   1. count == 0 AND dst == NULL AND size == 0 -> return 0, no handler, nothing written;
;   2. dst == NULL or size == 0                 -> handler, EINVAL (22), dst untouched;
;   3. count == 0                               -> dst[0] = 0, return 0, and NO handler even when
;                                                  src is NULL (the count test precedes the src one);
;   4. src == NULL                              -> dst[0] = 0, handler, EINVAL;
;   5. it fits                                  -> exactly n+1 wide characters written, return 0;
;   6. does not fit, count != _TRUNCATE         -> `size` wchars written FIRST, then dst[0] = 0,
;                                                  handler, ERANGE (34);
;   7. does not fit, count == _TRUNCATE         -> `size` wchars written, then dst[size-1] = 0,
;                                                  return STRUNCATE (80) and NO handler.
;
; ---- one wrinkle the narrow version does not have ----------------------------------------------
; `_TRUNCATE` is (size_t)-1, so doubling `count` to a byte count would wrap it to -2 and destroy the
; sentinel. So count is compared against size in WIDE units first, and only the winner, the bound
; lim = min(count, size), which is at most `size` and therefore a real length, is doubled. The
; doubling itself uses the saturating `shl / sbb / or` from change 151, so an absurd size near 2^63
; clamps instead of wrapping into a spurious ERANGE. The original `count` stays untouched in r9,
; which is what the failure path later tests against -1 to pick between cases 6 and 7.
;
; Everything else is 154 with `vpcmpeqb` -> `vpcmpeqw`: since `vpcmpeqw` sets both bytes of a
; matching word, `tzcnt` lands on the low (even) byte and the index it produces is already a byte
; offset, so the scan, the bound comparison and the copy all count bytes uniformly.
;
; ISA: AVX2 + BMI2 (shrx). Validated on Zen3.

EXTERN _invalid_parameter_noinfo:PROC

; --- copy exactly r10 bytes from r8 to r11 (r10 >= 2, always even); clobbers rax, rcx, rdx, ymm0 ---
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
        mov       rdx, r10
cloop:  cmp       rdx, 32
        jbe       clast
        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymmword ptr [rax], ymm0
        add       rcx, 32
        add       rax, 32
        sub       rdx, 32
        jmp       cloop
clast:  vmovdqu   ymm0, ymmword ptr [r8 + r10 - 32]
        vmovdqu   ymmword ptr [r11 + r10 - 32], ymm0
cdone:
ENDM

; --- find the first zero WORD in src[0, rdx) bytes; byte index in rax, else jump to `notfound` ----
; ymm1 must already be zero. Clobbers rax, rcx, r10, ymm0. Preserves r8, r9, r11, rdx.
SCAN MACRO notfound
    LOCAL lo, hi, nxt, have
        mov       r10, r8
        and       r10, -32
        mov       ecx, r8d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, ecx                     ; bit i means byte i of src is in a zero word
        neg       ecx
        add       ecx, 32
        test      eax, eax
        jnz       lo
nxt:    cmp       rcx, rdx
        jae       notfound
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       hi
        add       rcx, 32
        jmp       nxt
hi:     tzcnt     eax, eax
        add       rax, rcx
        jmp       have
lo:     tzcnt     eax, eax
have:   cmp       rax, rdx
        jae       notfound
ENDM

.code
wia_wcsncpy_s PROC
        test      r9, r9
        jnz       ws_counted

        ; ---- count == 0 -------------------------------------------------------------------------
        test      rcx, rcx
        jnz       ws_c0_dst
        test      rdx, rdx
        jz        ws_ok                             ; the documented all-NULL no-op
        jmp       ws_einval
ws_c0_dst:
        test      rdx, rdx
        jz        ws_einval
        mov       word ptr [rcx], 0                 ; no handler here, even for a NULL src
        xor       eax, eax
        ret

ws_counted:
        test      rcx, rcx
        jz        ws_einval
        test      rdx, rdx
        jz        ws_einval
        mov       r11, rcx                          ; dst
        test      r8, r8
        jz        ws_src_null

        vpxor     ymm1, ymm1, ymm1
        cmp       r9, rdx                           ; count vs size, still in WIDE units
        jae       ws_bound_size

        ; ---- lim = count (< size): even "no NUL" fits ------------------------------------------
        mov       rdx, r9
        shl       rdx, 1
        sbb       rax, rax
        or        rdx, rax                          ; saturating wchars -> bytes
        SCAN ws_take_bound
        jmp       ws_fits
ws_take_bound:
        mov       rax, rdx                          ; n = lim, in bytes
        jmp       ws_fits

        ; ---- lim = size: "no NUL" is the truncation case ---------------------------------------
ws_bound_size:
        shl       rdx, 1
        sbb       rax, rax
        or        rdx, rax
        SCAN ws_nofit

ws_fits:
        ; rax = n in bytes, and n < size*2. Write n bytes then the terminating word.
        test      rax, rax
        jz        ws_empty
        mov       r9, r11
        add       r9, rax                           ; where the terminator goes
        mov       r10, rax
        COPYN
        mov       word ptr [r9], 0
        xor       eax, eax
        vzeroupper
        ret
ws_empty:
        mov       word ptr [r11], 0
        xor       eax, eax
        vzeroupper
        ret

ws_nofit:
        ; rdx == size*2 here (this path is only reachable when lim was size)
        cmp       r9, -1
        je        ws_truncate

        ; ---- ERANGE: write `size` wchars, then empty the string ---------------------------------
        mov       r10, rdx
        mov       r9, r11
        COPYN
        mov       word ptr [r9], 0
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

        ; ---- _TRUNCATE: write `size` wchars, terminate the LAST one, stay silent ----------------
ws_truncate:
        lea       r9, [r11 + rdx - 2]
        mov       r10, rdx
        COPYN
        mov       word ptr [r9], 0
        mov       eax, 80                           ; STRUNCATE, no handler
        vzeroupper
        ret

ws_src_null:
        mov       word ptr [r11], 0
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

ws_einval:
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

ws_ok:
        xor       eax, eax
        ret
wia_wcsncpy_s ENDP
END
