; changes/154-strncpy-s/impl.asm
; errno_t wia_strncpy_s(char* dst, rsize_t size, const char* src, rsize_t count)
;   [Win64: rcx, rdx, r8, r9]
;
; Reimplements ucrtbase!strncpy_s. Like the rest of the _s family (changes 150-153) the live one is
; the UCRT scalar loop, one byte per iteration with two counters:
;     while (count > 0 && (*p++ = *src++) != 0 && --available > 0) --count;
; 66 ns for 254 characters, where ucrtbase's own plain strcpy does the same string in 16 ns.
;
; Contract (probed against the live export):
;   1. count == 0 AND dst == NULL AND size == 0 -> return 0, no handler, nothing written. This is a
;      documented no-op and needs all three; dst == NULL with size != 0 is still EINVAL.
;   2. dst == NULL or size == 0                 -> handler, EINVAL (22), dst untouched;
;   3. count == 0                               -> dst[0] = 0, return 0, and NO handler even when
;      src is NULL -- the count test comes before the src test, which the probe confirms;
;   4. src == NULL                              -> dst[0] = 0, handler, EINVAL;
;   5. it fits                                  -> exactly n+1 bytes written, return 0;
;   6. it does not fit, count != _TRUNCATE      -> `size` bytes written FIRST, then dst[0] = 0,
;      handler, ERANGE (34);
;   7. it does not fit, count == _TRUNCATE      -> `size` bytes written, then dst[size-1] = 0,
;      return STRUNCATE (80) and NO handler. Probed: size 4, "abcdef", _TRUNCATE leaves 61 62 63 00.
;
; The two truncation paths differ in where they write the terminator AND in whether the handler
; fires, so they are kept separate rather than folded.
;
; ---- how cases 5-7 collapse to one test --------------------------------------------------------
; Let lim = min(count, size) and let n be the index of the first NUL in src[0, lim), or lim if there
; is none. Then the whole thing is:
;
;     NUL found at j < lim  ->  n = j, always a success (j < lim <= size)
;     no NUL, lim == count  ->  n = count, and count < size here, so also a success
;     no NUL, lim == size   ->  it does not fit
;
; which is why the code picks the branch by comparing count against size ONCE, up front, and the
; scan's "not found" exit then means something different in each -- so the bound register can hold
; lim and still be a valid `size` on the failure path, because that path is only reachable when
; lim == size. That is what keeps seven live values inside the volatile registers.
;
; Reads: the scan uses 32-byte ALIGNED loads (align down, shift the leading bytes out of the mask)
; and stops after the block holding index lim-1, so it never crosses into a page the caller did not
; give us and never reads further than the live scalar loop would. Every byte the copy then reads
; has already been touched by the scan.
; Writes: exactly n+1 bytes on success, exactly `size` on failure -- the head/tail pair overlaps
; INSIDE the copied range, so nothing past the last byte the live function writes is disturbed.
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

; --- find the first NUL in src[0, rdx); result index in rax, else jump to `notfound` --------------
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
        shrx      eax, eax, ecx                     ; bit i means src[i] == 0
        neg       ecx
        add       ecx, 32                           ; bytes of src this first block covered
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
        jae       notfound                          ; a NUL at or past the bound does not count
ENDM

.code
wia_strncpy_s PROC
        test      r9, r9
        jnz       ns_counted

        ; ---- count == 0 -------------------------------------------------------------------------
        test      rcx, rcx
        jnz       ns_c0_dst
        test      rdx, rdx
        jz        ns_ok                             ; the documented all-NULL no-op
        jmp       ns_einval
ns_c0_dst:
        test      rdx, rdx
        jz        ns_einval
        mov       byte ptr [rcx], 0                 ; no handler here, even for a NULL src
        xor       eax, eax
        ret

ns_counted:
        test      rcx, rcx
        jz        ns_einval
        test      rdx, rdx
        jz        ns_einval
        mov       r11, rcx                          ; dst
        test      r8, r8
        jz        ns_src_null

        vpxor     ymm1, ymm1, ymm1
        cmp       r9, rdx
        jae       ns_bound_size

        ; ---- lim = count (< size): even "no NUL" fits ------------------------------------------
        mov       rdx, r9
        SCAN ns_take_bound
        jmp       ns_fits
ns_take_bound:
        mov       rax, rdx                          ; n = count
        jmp       ns_fits

        ; ---- lim = size: "no NUL" is the truncation case ---------------------------------------
ns_bound_size:
        SCAN ns_nofit

ns_fits:
        ; rax = n, and n < size. Write n bytes then the terminator.
        test      rax, rax
        jz        ns_empty
        mov       r9, r11
        add       r9, rax                           ; where the terminator goes
        mov       r10, rax
        COPYN
        mov       byte ptr [r9], 0
        xor       eax, eax
        vzeroupper
        ret
ns_empty:
        mov       byte ptr [r11], 0
        xor       eax, eax
        vzeroupper
        ret

ns_nofit:
        ; rdx == size here (this path is only reachable when lim was size)
        cmp       r9, -1
        je        ns_truncate

        ; ---- ERANGE: write `size` bytes, then empty the string ---------------------------------
        mov       r10, rdx
        mov       r9, r11                           ; keep dst for the reset
        COPYN
        mov       byte ptr [r9], 0
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

        ; ---- _TRUNCATE: write `size` bytes, terminate the LAST one, and stay silent ------------
ns_truncate:
        lea       r9, [r11 + rdx - 1]
        mov       r10, rdx
        COPYN
        mov       byte ptr [r9], 0
        mov       eax, 80                           ; STRUNCATE, no handler
        vzeroupper
        ret

ns_src_null:
        mov       byte ptr [r11], 0
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

ns_einval:
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

ns_ok:
        xor       eax, eax
        ret
wia_strncpy_s ENDP
END
