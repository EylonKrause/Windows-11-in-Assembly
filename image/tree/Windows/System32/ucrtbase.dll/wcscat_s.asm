; ucrtbase.dll!wcscat_s  --  hand-written x86-64 reimplementation (4.30x vs shipped)
; source of truth: changes/153-wcscat-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/153-wcscat-s/impl.asm
; errno_t wia_wcscat_s(wchar_t* dst, rsize_t size, const wchar_t* src)   [Win64: rcx, rdx, r8]
;
; The wide twin of change 152, with `size` counted in wchar_t. ucrtbase!wcscat_s is the same pair of
; scalar loops, one wide character per iteration.
;
; Contract, identical to strcat_s in wide units:
;   1. dst == NULL or size == 0      -> handler, EINVAL (22), dst untouched;
;   2. no terminator in dst[0..size) -> dst[0] = 0, handler, EINVAL, and only dst[0] is written;
;   3. src == NULL                   -> dst[0] = 0, handler, EINVAL;
;   4. src fits in the remainder     -> exactly len+1 wchars written at dst+L, return 0;
;   5. src does not fit              -> exactly `size - L` wchars appended FIRST, then dst[0] = 0 on
;                                       the ORIGINAL dst, then the handler, then ERANGE (34).
; As in 152 the NULL-src test is hoisted above the dst scan so the two scans can be issued together;
; cases 2 and 3 are observationally identical (same bytes, same handler count, same return), so the
; order between them is not observable.
;
; `size` is doubled to bytes once, up front, with the saturating `shl / sbb / or` from change 151 so
; an absurd size near 2^63 clamps instead of wrapping into a spurious ERANGE. Everything after that
; is 152 with `vpcmpeqb` -> `vpcmpeqw`: `vpcmpeqw` sets both bytes of a matching word, so `tzcnt`
; lands on the low (even) byte and the indices it produces are already byte offsets.
;
; The scalar `dst[0]` test in front of the vector loads is the same trick as 152 and matters for the
; same reason; a caller's `dst[0] = 0` leaves a small store that a 32-byte load over those bytes
; cannot forward from (~24 cycles on Zen3), while a 2-byte load forwards cleanly.
;
; ISA: AVX2 + BMI2. Validated on Zen3.

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
clast:  vmovdqu   ymm0, ymmword ptr [r8 + r10 - 32]   ; final 32, overlapping backwards inside [0,n)
        vmovdqu   ymmword ptr [r11 + r10 - 32], ymm0
cdone:
ENDM

.code
wia_wcscat_s PROC
        test      rcx, rcx
        jz        wt_einval_nowrite
        test      rdx, rdx
        jz        wt_einval_nowrite
        shl       rdx, 1                            ; size: wchars -> bytes ...
        sbb       rax, rax                          ; ... saturating, so an absurd size clamps
        or        rdx, rax
        test      r8, r8
        jz        wt_src_null_early

        vpxor     ymm1, ymm1, ymm1
        mov       r11, rcx                          ; dst base / append point
        mov       r9, rcx                           ; dst base, kept for the reset
        cmp       word ptr [rcx], 0
        jne       wt_scan_dst
        ; ---- dst is empty: L = 0, so this is exactly wcscpy_s and dst is never loaded wide ------
        jmp       wt_sfull                          ; append point = dst, available = size

        ; ---- both first blocks, issued together ------------------------------------------------
wt_scan_dst:
        mov       r10, r8
        and       r10, -32
        vmovdqa   ymm2, ymmword ptr [r10]           ; src block first: it cannot be stalled by the
        mov       r9, rcx                           ;   caller's terminator store into dst
        and       r9, -32
        vmovdqa   ymm0, ymmword ptr [r9]            ; dst block
        mov       eax, r8d
        and       eax, 31                           ; src offset within its block (even)
        and       ecx, 31                           ; dst offset within its block (even)
        vpcmpeqw  ymm2, ymm2, ymm1
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb r10d, ymm2
        vpmovmskb r9d, ymm0
        shrx      r10d, r10d, eax                   ; bit i means byte i of src is in a zero word
        shrx      r9d, r9d, ecx                     ; bit i means byte i of dst is in a zero word
        test      r9d, r9d
        jz        wt_dslow
        test      r10d, r10d
        jz        wt_sslow

        ; ---- fast path: both terminators sit inside their own first block ----------------------
        tzcnt     ecx, r9d                          ; byte offset of dst's terminator (even)
        cmp       rcx, rdx
        jae       wt_notterm
        mov       r9, rdx
        sub       r9, rcx                           ; available, in bytes (>= 2)
        tzcnt     eax, r10d                         ; byte offset of src's terminator
        cmp       rax, r9
        jae       wt_erange_fast
        add       r11, rcx                          ; append point
        lea       r10, [rax + 2]                    ; append the terminator too
        COPYN
        xor       eax, eax
        vzeroupper
        ret

wt_erange_fast:
        mov       r10, r9                           ; write exactly `available` bytes
        mov       r9, r11                           ; keep the ORIGINAL dst for the reset
        add       r11, rcx
        COPYN
        mov       word ptr [r9], 0
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

        ; ---- dst terminator is past its first block --------------------------------------------
wt_dslow:
        mov       r9, r11
        and       r9, -32
        neg       ecx
        add       ecx, 32                           ; rcx = bytes of dst the first block covered
wt_dnext:
        cmp       rcx, rdx
        jae       wt_notterm
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       wt_dhi
        add       rcx, 32
        jmp       wt_dnext
wt_dhi: tzcnt     eax, eax
        add       rax, rcx
        cmp       rax, rdx
        jae       wt_notterm
        mov       r9, r11                           ; dst base, kept for the reset
        add       r11, rax                          ; append point
        sub       rdx, rax                          ; available

        ; ---- src prologue: reached from the empty-dst shortcut and from the slow dst scan -------
wt_sfull:
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
        jnz       wt_slo
        jmp       wt_snext

        ; ---- dst was short but the src terminator is past its first block ----------------------
wt_sslow:
        tzcnt     ecx, r9d
        cmp       rcx, rdx
        jae       wt_notterm
        mov       r9, r11                           ; dst base, kept for the reset
        add       r11, rcx                          ; append point
        sub       rdx, rcx                          ; available
        mov       r10, r8
        and       r10, -32
        neg       eax
        add       eax, 32
        mov       ecx, eax                          ; bytes of src the first block covered

wt_snext:
        cmp       rcx, rdx
        jae       wt_erange
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       wt_shi
        add       rcx, 32
        jmp       wt_snext
wt_shi: tzcnt     eax, eax
        add       rax, rcx
        jmp       wt_shave
wt_slo: tzcnt     eax, eax
wt_shave:
        cmp       rax, rdx
        jae       wt_erange
        lea       r10, [rax + 2]
        COPYN
        xor       eax, eax
        vzeroupper
        ret

wt_erange:
        mov       r10, rdx                          ; write exactly `available` bytes
        COPYN
        mov       word ptr [r9], 0                  ; the ORIGINAL dst, not the append point
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

wt_notterm:
        mov       word ptr [r11], 0                 ; r11 is still the original dst on every path here
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

wt_src_null_early:
        mov       word ptr [rcx], 0
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

wt_einval_nowrite:
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret
wia_wcscat_s ENDP
END
