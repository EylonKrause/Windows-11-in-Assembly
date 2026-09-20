; ucrtbase.dll!strcpy_s  --  hand-written x86-64 reimplementation (3.86x vs shipped)
; source of truth: changes/150-strcpy-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/150-strcpy-s/impl.asm
; errno_t wia_strcpy_s(char* dst, rsize_t size, const char* src)   [Win64: rcx, rdx, r8]
;
; Reimplements ucrtbase!strcpy_s. The live one is the textbook UCRT scalar loop
;     while ((*p++ = *src++) != 0 && --available > 0) {}
; i.e. one byte per iteration with a bound check, 65 ns for 254 bytes, while ucrtbase's own plain
; strcpy does the same string in 16 ns. This is an AVX2 bounded NUL scan followed by an exact-length
; copy.
;
; Contract (probed against the live export, and matching the UCRT sources):
;   - dst == NULL or size == 0  -> invalid-parameter handler, return EINVAL (22), dst untouched;
;   - src == NULL               -> dst[0] = 0, handler, return EINVAL;
;   - src fits                  -> exactly len+1 bytes written, return 0;
;   - src does NOT fit          -> exactly `size` bytes of src are written FIRST, then dst[0] = 0,
;                                  then the handler, then ERANGE (34). The partial copy is observable
;                                  (size = 5, src = "abcdefghij" leaves 00 62 63 64 65) so it is
;                                  reproduced byte for byte rather than short-circuited.
;   - the handler is invoked through ucrtbase's own exported `_invalid_parameter_noinfo`, so the error
;     paths are indistinguishable from the live ones, including the default handler's __fastfail
;     when the process installed none. All five handler arguments are NULL in the live release CRT,
;     which is exactly what that export passes.
;
; Reads: the scan uses 32-byte ALIGNED loads (align down, shift the leading bytes out of the mask), so
; it never crosses a page the caller did not give us, and it stops after the block holding index
; size-1. Every byte the copy then reads has already been touched by the scan, so the copy cannot
; fault where the live scalar loop would not.
; Writes: exactly n bytes, never n rounded up, the head/tail pair overlaps INSIDE [0, n), so nothing
; past the last byte the live function writes is disturbed.
;
; ISA: AVX2. Validated on Zen3.

EXTERN _invalid_parameter_noinfo:PROC

; --- copy exactly r10 bytes from r8 to r11 (r10 >= 1); clobbers rax, rcx, r9, ymm0 ---------------
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
wia_strcpy_s PROC
        test      rcx, rcx
        jz        cs_einval_nowrite
        test      rdx, rdx
        jz        cs_einval_nowrite
        mov       r11, rcx                          ; r11 = dst  (rcx is needed for the shift count)
        test      r8, r8
        jz        cs_src_null

        ; ---- bounded NUL scan, page-safe -------------------------------------------------------
        vpxor     ymm1, ymm1, ymm1
        mov       r9, r8
        and       r9, -32                           ; align the first load down
        mov       ecx, r8d
        and       ecx, 31                           ; bytes of that block that precede src
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shr       eax, cl                           ; bit i now means src[i] == 0
        mov       r10d, 32
        sub       r10d, ecx                         ; bytes of src covered by the prologue block
        test      eax, eax
        jnz       cs_found_lo

cs_next:
        cmp       r10, rdx
        jae       cs_erange                         ; covered every byte below `size`, no NUL there
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       cs_found_hi
        add       r10, 32
        jmp       cs_next

cs_found_hi:
        tzcnt     eax, eax
        add       rax, r10
        jmp       cs_have
cs_found_lo:
        tzcnt     eax, eax
cs_have:
        cmp       rax, rdx
        jae       cs_erange                         ; the NUL sits at or past `size` -> it never fits
        lea       r10, [rax + 1]                    ; copy the terminator too
        COPYN
        xor       eax, eax
        vzeroupper
        ret

        ; ---- src does not fit: copy exactly `size` bytes, empty dst, then ERANGE ----------------
cs_erange:
        mov       r10, rdx
        COPYN
        mov       byte ptr [r11], 0
        vzeroupper
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 34                           ; ERANGE
        ret

cs_src_null:
        mov       byte ptr [r11], 0
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret

cs_einval_nowrite:
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22                           ; EINVAL
        ret
wia_strcpy_s ENDP
END
