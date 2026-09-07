; changes/156-strncat-s/impl.asm
; errno_t wia_strncat_s(char* dst, rsize_t size, const char* src, rsize_t count)
;   [Win64: rcx, rdx, r8, r9]
;
; Reimplements ucrtbase!strncat_s -- the last of the bounded string family (150-155). The live one is
; a bounded strlen over dst followed by the same two-counter scalar loop as strncpy_s, 72 ns to
; append 254 characters.
;
; Contract (probed against the live export). It is strncat_s, not strncpy_s: three of these differ
; from change 154 and each was confirmed directly.
;   1. count == 0 AND dst == NULL AND size == 0 -> return 0, no handler, nothing written;
;   2. dst == NULL or size == 0                 -> handler, EINVAL (22), dst untouched;
;   3. count == 0 AND src == NULL               -> return 0, NOTHING WRITTEN and no handler -- not
;      even the terminator, and the dst walk does not run, so an unterminated dst is not reported;
;   4. src == NULL (count != 0)                 -> dst[0] = 0, handler, EINVAL;
;   5. no terminator in dst[0..size)            -> dst[0] = 0, handler, EINVAL, and ONLY dst[0] is
;      written. This is reached with count == 0 too, as long as src is non-NULL;
;   6. it fits                                  -> exactly n+1 bytes written at dst+L, return 0.
;      count == 0 lands here with n = 0, so it rewrites the existing terminator -- a write, but not
;      an observable change;
;   7. does not fit, count != _TRUNCATE         -> `available` bytes appended FIRST, then dst[0] = 0
;      on the ORIGINAL dst, handler, ERANGE (34);
;   8. does not fit, count == _TRUNCATE         -> `available` bytes appended, then dst[size-1] = 0,
;      return STRUNCATE (80) and NO handler.
;
; Probed: dst = "AB", size = 5, "xyz", count 3 leaves 00 42 78 79 7A and raises ERANGE, while the
; same call with _TRUNCATE leaves 41 42 78 79 00 and returns 80 quietly.
;
; ---- structure ----------------------------------------------------------------------------------
; A bounded strlen over dst gives L and available = size - L, and from there this is change 154 with
; `available` in place of `size`: with lim = min(count, available) and n the index of the first NUL
; in src[0, lim) (or lim if there is none),
;
;     NUL found at j < lim  ->  n = j, always a success
;     no NUL, lim == count  ->  n = count, and count < available here, so also a success
;     no NUL, lim == available -> it does not fit
;
; The failure paths need an address the scan is about to clobber -- dst for ERANGE, dst+size-1 for
; STRUNCATE -- and which one is known BEFORE the scan runs, because it depends only on `count`. So
; the branch is taken first and the surviving address parked in r9, which is how eight contract paths
; fit in the volatile registers with no stack frame and no non-volatile saves.
;
; ISA: AVX2 + BMI2 (shrx). Validated on Zen3.

EXTERN _invalid_parameter_noinfo:PROC

; --- copy exactly r10 bytes from r8 to r11 (r10 >= 1); clobbers rax, rcx, rdx, ymm0 ---------------
COPYN MACRO
    LOCAL c16, c8, c4, c2, c1, cbig, cloop, clast, cdone
        cmp       r10, 32
        jae       cbig
        cmp       r10, 16
        jae       c16
        cmp       r10, 8
        jae       c8
        cmp       r10, 4
        jae       c4
        cmp       r10, 2
        jae       c2
c1:     movzx     eax, byte ptr [r8]
        mov       byte ptr [r11], al
        jmp       cdone
c2:     movzx     eax, word ptr [r8]
        mov       word ptr [r11], ax
        movzx     ecx, word ptr [r8 + r10 - 2]
        mov       word ptr [r11 + r10 - 2], cx
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

; --- bounded strlen of dst (rcx) within rdx bytes; L in rax, else jump to `notterm` ---------------
; ymm1 must already be zero. Clobbers rax, r10, r11, ymm0. Preserves rcx, rdx, r8, r9.
WALKD MACRO notterm
    LOCAL lo, hi, nxt, have
        mov       r10, rcx
        and       r10, -32
        mov       r11d, ecx
        and       r11d, 31
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, r11d                    ; bit i means dst[i] == 0
        neg       r11d
        add       r11d, 32                          ; bytes of dst this first block covered
        test      eax, eax
        jnz       lo
nxt:    cmp       r11, rdx
        jae       notterm
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm0, ymm0, ymm1
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

; --- find the first NUL in src[0, rdx); index in rax, else jump to `notfound` ---------------------
; ymm1 must already be zero. Clobbers rax, rcx, r10, ymm0. Preserves r8, r9, r11, rdx.
SCAN MACRO notfound
    LOCAL lo, hi, nxt, have
        mov       r10, r8
        and       r10, -32
        mov       ecx, r8d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm0, ymm0, ymm1
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
        vpcmpeqb  ymm0, ymm0, ymm1
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
wia_strncat_s PROC
        test      r9, r9
        jnz       nc_have_count

        ; ---- count == 0 -------------------------------------------------------------------------
        test      rcx, rcx
        jnz       nc_c0_dst
        test      rdx, rdx
        jz        nc_ok                             ; the documented all-NULL no-op
        jmp       nc_einval
nc_c0_dst:
        test      rdx, rdx
        jz        nc_einval
        test      r8, r8
        jz        nc_ok                             ; NULL src with count 0: write nothing at all
        jmp       nc_walk                           ; otherwise the dst walk still runs

nc_have_count:
        test      rcx, rcx
        jz        nc_einval
        test      rdx, rdx
        jz        nc_einval
        test      r8, r8
        jz        nc_src_null

nc_walk:
        vpxor     ymm1, ymm1, ymm1
        ; A ONE-BYTE test before any vector load. The `dst[0] = 0; strncat_s(dst, ...)` idiom leaves
        ; a 1-byte store in flight that a 32-byte load over the same bytes cannot forward from --
        ; about 24 cycles on Zen3, squarely on the critical path because everything downstream needs
        ; L, with nothing to overlap it against. A one-byte load forwards cleanly, and an empty dst
        ; means L = 0 so the walk is not needed at all. Same fix, same reason, as change 152.
        cmp       byte ptr [rcx], 0
        jne       nc_walk_vector
        xor       eax, eax                          ; L = 0; size >= 1, so this always fits
        jmp       nc_walked
nc_walk_vector:
        WALKD nc_notterm
nc_walked:
        ; rax = L
        lea       r10, [rcx + rdx - 1]              ; dst + size - 1, for the STRUNCATE terminator
        mov       r11, rcx
        add       r11, rax                          ; append point dst + L
        sub       rdx, rax                          ; available = size - L  (>= 1)

        cmp       r9, -1
        je        nc_trunc_path
        cmp       r9, rdx
        jae       nc_bound_avail

        ; ---- lim = count (< available): even "no NUL" fits -------------------------------------
        mov       rdx, r9
        SCAN nc_take_bound
        jmp       nc_fits
nc_take_bound:
        mov       rax, rdx                          ; n = count
        jmp       nc_fits

        ; ---- lim = available, counted: "no NUL" is ERANGE ---------------------------------------
nc_bound_avail:
        mov       r9, rcx                           ; park the reset address before the scan
        SCAN nc_erange
        jmp       nc_fits

        ; ---- lim = available, _TRUNCATE: "no NUL" is STRUNCATE ----------------------------------
nc_trunc_path:
        mov       r9, r10                           ; park dst + size - 1
        SCAN nc_trunc

nc_fits:
        test      rax, rax
        jz        nc_empty
        mov       r9, r11
        add       r9, rax                           ; where the terminator goes
        mov       r10, rax
        COPYN
        mov       byte ptr [r9], 0
        xor       eax, eax
        vzeroupper
        ret
nc_empty:
        mov       byte ptr [r11], 0                 ; count == 0 rewrites the existing terminator
        xor       eax, eax
        vzeroupper
        ret

nc_erange:
        mov       r10, rdx                          ; append exactly `available` bytes
        COPYN
        mov       byte ptr [r9], 0                  ; empty the ORIGINAL string
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

nc_trunc:
        mov       r10, rdx
        COPYN
        mov       byte ptr [r9], 0                  ; terminate the last byte of the buffer
        mov       eax, 80                           ; STRUNCATE, no handler
        vzeroupper
        ret

nc_notterm:
        mov       byte ptr [rcx], 0
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

nc_src_null:
        mov       byte ptr [rcx], 0
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

nc_einval:
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

nc_ok:
        xor       eax, eax
        ret
wia_strncat_s ENDP
END
