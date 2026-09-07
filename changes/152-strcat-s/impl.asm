; changes/152-strcat-s/impl.asm
; errno_t wia_strcat_s(char* dst, rsize_t size, const char* src)   [Win64: rcx, rdx, r8]
;
; ucrtbase!strcat_s is two scalar byte loops back to back -- a bounded strlen over dst, then the same
; bounded copy loop as strcpy_s (change 150). Appending 254 characters costs 74 ns, and appending 16
; characters to a 1000-character string costs 236 ns. Both loops become AVX2 block scans here.
;
; Contract (probed against the live export; it is the UCRT template):
;   1. dst == NULL or size == 0      -> handler, EINVAL (22), dst untouched;
;   2. no terminator in dst[0..size) -> dst[0] = 0, handler, EINVAL   <-- extra path vs strcpy_s;
;   3. src == NULL                   -> dst[0] = 0, handler, EINVAL;
;   4. src fits in the remainder     -> exactly len+1 bytes written at dst+L, return 0;
;   5. src does not fit              -> exactly `size - L` bytes of src written at dst+L FIRST, then
;                                       dst[0] = 0 (the ORIGINAL dst, not the append point), handler,
;                                       ERANGE (34).
; Probed: dst = "AB", size = 3, src = "xyz" leaves 00 42 78 -- one byte of src appended and then the
; string emptied. Case 2 writes ONLY dst[0]; the rest of the buffer is left alone.
;
; Ordering note: cases 2 and 3 are checked here in the opposite order to the UCRT source, so that the
; NULL-src test can be hoisted above the dst scan and the two scans can then be issued together. That
; is safe because the two paths are OBSERVATIONALLY IDENTICAL -- both write dst[0] = 0, invoke the
; handler exactly once, and return EINVAL -- which the probe confirms for a dst that is unterminated
; AND a NULL src (rc=22, iph=1, dst = 00 5A ...). Nothing else distinguishes them.
;
; The two scans are INDEPENDENT, so both first blocks are loaded, compared and reduced to masks
; before either result is examined. That matters more than it looks: a caller that has just written
; a terminator into dst (`dst[0] = 0; strcat_s(dst, n, s);`, or a previous append) leaves a small
; store in flight that a 32-byte load over the same bytes cannot forward from -- about 12 cycles on
; Zen3. Serialised, that stall is pure added latency; overlapped with the src scan it is nearly free.
; The src block is loaded FIRST for the same reason: it is the load that cannot be stalled by the
; caller's store.
;
; The handler is reached through ucrtbase's own exported `_invalid_parameter_noinfo`, so the error
; paths are indistinguishable from the live ones, default __fastfail included.
;
; ISA: AVX2 + BMI2 (`shrx`, so the two masks can be shifted by two different counts without both
; needing cl). Validated on Zen3.

EXTERN _invalid_parameter_noinfo:PROC

; --- copy exactly r10 bytes from r8 to r11 (r10 >= 1); clobbers rax, rcx, rdx, ymm0 ---------------
; Counts in rdx rather than r9 because r9 has to keep the original dst alive across the copy, so the
; ERANGE path can empty the right string.
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
clast:  vmovdqu   ymm0, ymmword ptr [r8 + r10 - 32]   ; final 32, overlapping backwards inside [0,n)
        vmovdqu   ymmword ptr [r11 + r10 - 32], ymm0
cdone:
ENDM

.code
wia_strcat_s PROC
        test      rcx, rcx
        jz        ct_einval_nowrite
        test      rdx, rdx
        jz        ct_einval_nowrite
        test      r8, r8
        jz        ct_src_null_early

        vpxor     ymm1, ymm1, ymm1
        mov       r11, rcx                          ; dst base / append point
        mov       r9, rcx                           ; dst base, kept for the reset
        cmp       byte ptr [rcx], 0
        jne       ct_scan_dst
        ; ---- dst is empty: L = 0, so this is exactly strcpy_s and no load of dst is needed -----
        ; A ONE-BYTE load, which forwards cleanly from the caller's terminator store; the 32-byte
        ; load below would not, and that stall (~24 cycles on Zen3) is otherwise the whole cost of
        ; the `dst[0] = 0; strcat_s(dst, n, s);` idiom.
        jmp       ct_sfull                          ; append point = dst, available = size

        ; ---- both first blocks, issued together ------------------------------------------------
ct_scan_dst:
        mov       r10, r8
        and       r10, -32
        vmovdqa   ymm2, ymmword ptr [r10]           ; src block first: it cannot be stalled by the
        mov       r9, rcx                           ;   caller's terminator store into dst
        and       r9, -32
        vmovdqa   ymm0, ymmword ptr [r9]            ; dst block
        mov       eax, r8d
        and       eax, 31                           ; src offset within its block
        and       ecx, 31                           ; dst offset within its block
        vpcmpeqb  ymm2, ymm2, ymm1
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb r10d, ymm2
        vpmovmskb r9d, ymm0
        shrx      r10d, r10d, eax                   ; bit i means src[i] == 0
        shrx      r9d, r9d, ecx                     ; bit i means dst[i] == 0
        test      r9d, r9d
        jz        ct_dslow
        test      r10d, r10d
        jz        ct_sslow

        ; ---- fast path: both terminators sit inside their own first block ----------------------
        tzcnt     ecx, r9d                          ; L = strlen(dst)
        cmp       rcx, rdx
        jae       ct_notterm
        mov       r9, rdx
        sub       r9, rcx                           ; available = size - L  (>= 1)
        tzcnt     eax, r10d                         ; strlen(src)
        cmp       rax, r9
        jae       ct_erange_fast
        add       r11, rcx                          ; append point dst + L
        lea       r10, [rax + 1]                    ; append the terminator too
        COPYN
        xor       eax, eax
        vzeroupper
        ret

ct_erange_fast:
        mov       r10, r9                           ; write exactly `available` bytes
        mov       r9, r11                           ; keep the ORIGINAL dst for the reset
        add       r11, rcx
        COPYN
        mov       byte ptr [r9], 0
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

        ; ---- dst terminator is past its first block: general bounded strlen --------------------
ct_dslow:
        mov       r9, r11
        and       r9, -32
        neg       ecx
        add       ecx, 32                           ; rcx = bytes of dst the first block covered
ct_dnext:
        cmp       rcx, rdx
        jae       ct_notterm
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       ct_dhi
        add       rcx, 32
        jmp       ct_dnext
ct_dhi: tzcnt     eax, eax
        add       rax, rcx                          ; L
        cmp       rax, rdx
        jae       ct_notterm
        mov       r9, r11                           ; dst base, kept for the reset
        add       r11, rax                          ; append point
        sub       rdx, rax                          ; available

        ; ---- src prologue: reached from the empty-dst shortcut and from the slow dst scan -------
ct_sfull:
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
        jnz       ct_slo
        jmp       ct_snext

        ; ---- dst was short but the src terminator is past its first block ----------------------
ct_sslow:
        tzcnt     ecx, r9d                          ; L
        cmp       rcx, rdx
        jae       ct_notterm
        mov       r9, r11                           ; dst base, kept for the reset
        add       r11, rcx                          ; append point
        sub       rdx, rcx                          ; available
        mov       r10, r8
        and       r10, -32
        neg       eax
        add       eax, 32
        mov       ecx, eax                          ; bytes of src the first block covered

ct_snext:
        cmp       rcx, rdx
        jae       ct_erange
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       ct_shi
        add       rcx, 32
        jmp       ct_snext
ct_shi: tzcnt     eax, eax
        add       rax, rcx
        jmp       ct_shave
ct_slo: tzcnt     eax, eax
ct_shave:
        cmp       rax, rdx
        jae       ct_erange
        lea       r10, [rax + 1]
        COPYN
        xor       eax, eax
        vzeroupper
        ret

ct_erange:
        mov       r10, rdx                          ; write exactly `available` bytes
        COPYN
        mov       byte ptr [r9], 0                  ; the ORIGINAL dst, not the append point
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

ct_notterm:
        mov       byte ptr [r11], 0                 ; r11 is still the original dst on every path here
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

ct_src_null_early:
        mov       byte ptr [rcx], 0
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

ct_einval_nowrite:
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret
wia_strcat_s ENDP
END
