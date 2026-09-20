; changes/157-wcsncat-s/impl.asm
; errno_t wia_wcsncat_s(wchar_t* dst, rsize_t size, const wchar_t* src, rsize_t count)
;   [Win64: rcx, rdx, r8, r9]
;
; The wide twin of change 156, closing the bounded string family (150-157). ucrtbase!wcsncat_s is a
; bounded wcslen over dst followed by the same two-counter scalar loop, one wide character per
; iteration -- 65 ns to append 254 wide characters.
;
; Contract: identical to strncat_s in wchar_t units, all eight paths, including the three that differ
; from wcsncpy_s -- `count == 0` with a NULL src writes nothing and skips the dst walk entirely, the
; same call with a VALID src still runs the walk and can still report an unterminated destination,
; and an unterminated dst writes only dst[0].
;
; ---- the two wide-only wrinkles ------------------------------------------------------------------
; `size` is doubled to bytes once, up front, with the saturating `shl / sbb / or` from change 151, so
; an absurd size near 2^63 clamps instead of wrapping into a spurious ERANGE.
;
; `count` is NOT doubled up front, because `_TRUNCATE` is (size_t)-1 and doubling would wrap the
; sentinel to -2. The _TRUNCATE branch is taken first, on the original value; only then is a real
; `count` doubled, and a carry out of that shift is treated exactly like "count exceeds the space
; available", which it does -- a count of 2^63 or more wide characters cannot be satisfied by any
; buffer. That turns an overflow check into a branch the code already needed.
;
; Everything else is 156 with `vpcmpeqb` -> `vpcmpeqw`. Since `vpcmpeqw` sets both bytes of a matching
; word, `tzcnt` lands on the low (even) byte, so both scans produce byte offsets and the walk, the
; bound arithmetic and the copy all count bytes uniformly.
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

; --- bounded wcslen of dst (rcx) within rdx BYTES; byte length in rax, else jump to `notterm` -----
WALKD MACRO notterm
    LOCAL lo, hi, nxt, have
        mov       r10, rcx
        and       r10, -32
        mov       r11d, ecx
        and       r11d, 31
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, r11d
        neg       r11d
        add       r11d, 32
        test      eax, eax
        jnz       lo
nxt:    cmp       r11, rdx
        jae       notterm
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       hi
        add       r11, 32
        jmp       nxt
hi:     tzcnt     eax, eax
        add       rax, r11
        jmp       have
lo:     tzcnt     eax, eax
have:   cmp       rax, rdx
        jae       notterm
ENDM

; --- find the first zero WORD in src[0, rdx) bytes; byte index in rax, else jump to `notfound` ----
SCAN MACRO notfound
    LOCAL lo, hi, nxt, have
        mov       r10, r8
        and       r10, -32
        mov       ecx, r8d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, ecx
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
wia_wcsncat_s PROC
        test      r9, r9
        jnz       wc_have_count

        ; ---- count == 0 -------------------------------------------------------------------------
        test      rcx, rcx
        jnz       wc_c0_dst
        test      rdx, rdx
        jz        wc_ok                             ; the documented all-NULL no-op
        jmp       wc_einval
wc_c0_dst:
        test      rdx, rdx
        jz        wc_einval
        test      r8, r8
        jnz       wc_walk                           ; src non-NULL: the ordinary dst walk
        ; a NULL source with count == 0 Still validates the destination -- the narrow sibling 156
        ; had the identical defect and the identical fix. Returning 0 here is right only when the
        ; destination is ALREADY a valid string within `size`: with dst = L"A" and size = 1 there
        ; is no terminator in dst[0..size), and the shipped export returns EINVAL, sets dst[0] = 0
        ; and calls the invalid-parameter handler. Found by live substitution on 3 of 16000 cases.
        vpxor     ymm1, ymm1, ymm1
        cmp       word ptr [rcx], 0
        je        wc_ok                             ; empty dst and size >= 1: valid, nothing to do
        WALKD wc_notterm                            ; no terminator in range -> dst[0]=0, EINVAL
        jmp       wc_ok                             ; terminated: write nothing at all

wc_have_count:
        test      rcx, rcx
        jz        wc_einval
        test      rdx, rdx
        jz        wc_einval
        test      r8, r8
        jz        wc_src_null

wc_walk:
        vpxor     ymm1, ymm1, ymm1
        shl       rdx, 1                            ; size: wchars -> bytes, saturating
        sbb       rax, rax
        or        rdx, rax
        ; A TWO-BYTE test before any vector load: the caller's `dst[0] = 0` leaves a narrow store
        ; that a 32-byte load over the same bytes cannot forward from (~24 cycles on Zen3), while a
        ; 2-byte load forwards cleanly -- and an empty dst means L = 0, so the walk is not needed.
        cmp       word ptr [rcx], 0
        jne       wc_walk_vector
        xor       eax, eax                          ; L = 0; size >= 1 wchar, so this always fits
        jmp       wc_walked
wc_walk_vector:
        WALKD wc_notterm
wc_walked:
        ; rax = L in bytes
        lea       r10, [rcx + rdx - 2]              ; &dst[size-1], for the STRUNCATE terminator
        mov       r11, rcx
        add       r11, rax                          ; append point
        sub       rdx, rax                          ; available, in bytes (>= 2)

        cmp       r9, -1
        je        wc_trunc_path
        mov       rax, r9
        shl       rax, 1                            ; count -> bytes
        jc        wc_bound_avail                    ; overflow: no buffer could satisfy it anyway
        cmp       rax, rdx
        jae       wc_bound_avail

        ; ---- lim = count (< available): even "no NUL" fits -------------------------------------
        mov       rdx, rax
        SCAN wc_take_bound
        jmp       wc_fits
wc_take_bound:
        mov       rax, rdx                          ; n = lim, in bytes
        jmp       wc_fits

        ; ---- lim = available, counted: "no NUL" is ERANGE ---------------------------------------
wc_bound_avail:
        mov       r9, rcx                           ; park the reset address before the scan
        SCAN wc_erange
        jmp       wc_fits

        ; ---- lim = available, _TRUNCATE: "no NUL" is STRUNCATE ----------------------------------
wc_trunc_path:
        mov       r9, r10                           ; park &dst[size-1]
        SCAN wc_trunc

wc_fits:
        test      rax, rax
        jz        wc_empty
        mov       r9, r11
        add       r9, rax                           ; where the terminator goes
        mov       r10, rax
        COPYN
        mov       word ptr [r9], 0
        xor       eax, eax
        vzeroupper
        ret
wc_empty:
        mov       word ptr [r11], 0                 ; count == 0 rewrites the existing terminator
        xor       eax, eax
        vzeroupper
        ret

wc_erange:
        mov       r10, rdx                          ; append exactly `available` bytes
        COPYN
        mov       word ptr [r9], 0                  ; empty the ORIGINAL string
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

wc_trunc:
        mov       r10, rdx
        COPYN
        mov       word ptr [r9], 0                  ; terminate the last wchar of the buffer
        mov       eax, 80                           ; STRUNCATE, no handler
        vzeroupper
        ret

wc_notterm:
        mov       word ptr [rcx], 0
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

wc_src_null:
        mov       word ptr [rcx], 0
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

wc_einval:
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

wc_ok:
        xor       eax, eax
        ret
wia_wcsncat_s ENDP
END
