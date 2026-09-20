; changes/294-strchr/probes/shapes.asm
;
; THE SHAPE EXPERIMENT. Six candidate implementations of wia_strchr, identical in contract and
; differing only in prologue width and main-loop width, so that the choice recorded in impl.asm is
; a measurement rather than an opinion. shapes.c times all six against the live ucrtbase export at
; sixteen lengths x three start alignments, absent and found-at-end, after screening every one of
; them for correctness against a scalar oracle and a PAGE_NOACCESS guard.
;
;   a1  two 128-bit blocks, then a 32-byte ymm loop          (the first version of impl.asm)
;   a2  a1, but the ymm loop loads once and compares twice instead of using two memory operands
;   a3  128-bit ramp to 64 alignment, then a 64-byte (2x32) ymm loop
;   a4  128-bit ramp to 64 alignment, then a 64-byte zmm loop           <- the shape that won
;   b2  an UNCONDITIONAL second 128-bit block, then `and -32`, then the 32-byte ymm loop
;   b3  four 128-bit blocks sharing ONE exit reached by `jnz`, then the 32-byte ymm loop
;
; b3 is kept deliberately even though it lost: its single shared exit is what identified the
; taken-branch cost that impl.asm now avoids by giving every block its own fall-through exit.
;
; Build (from a VS x64 environment):
;   ml64 /nologo /c /Fo shapesasm.obj shapes.asm
;   cl /nologo /O2 /I ..\..\..\harness shapes.c shapesasm.obj /Fe:shapes.exe

.code

; ============================================================ A1 : shipped candidate
wia_a1 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           a1_widen
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a1_null
        lea          rax, [r11 + r8]
        ret
a1_null:
        xor          eax, eax
        ret
a1_widen:
        add          r9, 16
        test         r9b, 31
        jz           a1_go256
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           a1_bump
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a1_null
        lea          rax, [r9 + r8]
        ret
a1_bump:
        add          r9, 16
a1_go256:
        vpbroadcastb ymm2, xmm2
        vpxor        ymm1, ymm1, ymm1
        sub          r9, 32
a1_scan:
        add          r9, 32
        vpcmpeqb     ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqb     ymm3, ymm1, ymmword ptr [r9]
        vpor         ymm4, ymm0, ymm3
        vpmovmskb    r8d, ymm4
        test         r8d, r8d
        jz           a1_scan
        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a1_nully
        lea          rax, [r9 + r8]
        vzeroupper
        ret
a1_nully:
        xor          eax, eax
        vzeroupper
        ret
wia_a1 ENDP

; ============================================================ A2 : one load + two reg compares
wia_a2 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           a2_widen
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a2_null
        lea          rax, [r11 + r8]
        ret
a2_null:
        xor          eax, eax
        ret
a2_widen:
        add          r9, 16
        test         r9b, 31
        jz           a2_go256
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           a2_bump
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a2_null
        lea          rax, [r9 + r8]
        ret
a2_bump:
        add          r9, 16
a2_go256:
        vpbroadcastb ymm2, xmm2
        vpxor        ymm1, ymm1, ymm1
        sub          r9, 32
a2_scan:
        add          r9, 32
        vmovdqa      ymm5, ymmword ptr [r9]
        vpcmpeqb     ymm0, ymm2, ymm5
        vpcmpeqb     ymm3, ymm1, ymm5
        vpor         ymm4, ymm0, ymm3
        vpmovmskb    r8d, ymm4
        test         r8d, r8d
        jz           a2_scan
        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a2_nully
        lea          rax, [r9 + r8]
        vzeroupper
        ret
a2_nully:
        xor          eax, eax
        vzeroupper
        ret
wia_a2 ENDP

; ============================================================ A3 : 64-byte unrolled ymm loop
wia_a3 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           a3_ramp
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a3_null
        lea          rax, [r11 + r8]
        ret
a3_null:
        xor          eax, eax
        ret
a3_ramp:                                       ; 128-bit blocks until r9 is 64-aligned (<=3)
        add          r9, 16
        test         r9b, 63
        jz           a3_go
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           a3_ramp
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a3_null
        lea          rax, [r9 + r8]
        ret
a3_go:
        vpbroadcastb ymm2, xmm2
        vpxor        ymm1, ymm1, ymm1
        sub          r9, 64
a3_scan:
        add          r9, 64
        vpcmpeqb     ymm0, ymm2, ymmword ptr [r9]        ; needle mask, low half
        vpcmpeqb     ymm3, ymm1, ymmword ptr [r9]
        vpor         ymm3, ymm0, ymm3                    ; stop mask, low half
        vpcmpeqb     ymm4, ymm2, ymmword ptr [r9+32]     ; needle mask, high half
        vpcmpeqb     ymm5, ymm1, ymmword ptr [r9+32]
        vpor         ymm5, ymm4, ymm5                    ; stop mask, high half
        vpor         ymm5, ymm3, ymm5                    ; any stop at all
        vpmovmskb    r8d, ymm5
        test         r8d, r8d
        jz           a3_scan
        vpmovmskb    r8d, ymm3                 ; stop mask of the LOW half
        test         r8d, r8d
        jnz          a3_lo                     ; ymm0 already holds its needle mask
        add          r9, 32
        vmovdqa      ymm0, ymm4                ; needle mask of the HIGH half
        vpcmpeqb     ymm5, ymm1, ymmword ptr [r9]
        vpor         ymm5, ymm0, ymm5
        vpmovmskb    r8d, ymm5
a3_lo:
        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a3_nully
        lea          rax, [r9 + r8]
        vzeroupper
        ret
a3_nully:
        xor          eax, eax
        vzeroupper
        ret
wia_a3 ENDP

; ============================================================ A4 : AVX-512 zmm loop (64 B / iter)
wia_a4 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           a4_ramp
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a4_null
        lea          rax, [r11 + r8]
        ret
a4_null:
        xor          eax, eax
        ret
a4_ramp:
        add          r9, 16
        test         r9b, 63
        jz           a4_go
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           a4_ramp
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          a4_null
        lea          rax, [r9 + r8]
        ret
a4_go:
        vpbroadcastb zmm16, xmm2
        sub          r9, 64
a4_scan:
        add          r9, 64
        vmovdqa64    zmm17, zmmword ptr [r9]
        vpcmpeqb     k1, zmm17, zmm16
        vptestnmb    k2, zmm17, zmm17
        kortestq     k1, k2
        jz           a4_scan
        korq         k3, k1, k2
        kmovq        rax, k3
        tzcnt        rax, rax
        kmovq        r10, k1
        bt           r10, rax
        jnc          a4_nullz
        add          rax, r9
        vzeroupper
        ret
a4_nullz:
        xor          eax, eax
        vzeroupper
        ret
wia_a4 ENDP

; ============================================================ B2 : unconditional 2nd 128-bit probe
;                                                                  then round DOWN to 32 (re-scan is
;                                                                  harmless: the bytes are proven clean)
wia_b2 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           b2_p2
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          b2_null
        lea          rax, [r11 + r8]
        ret
b2_null:
        xor          eax, eax
        ret
b2_p2:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           b2_go256
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          b2_null
        lea          rax, [r9 + r8]
        ret
b2_go256:
        add          r9, 16
        and          r9, -32
        vpbroadcastb ymm2, xmm2
        vpxor        ymm1, ymm1, ymm1
        sub          r9, 32
b2_scan:
        add          r9, 32
        vpcmpeqb     ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqb     ymm3, ymm1, ymmword ptr [r9]
        vpor         ymm4, ymm0, ymm3
        vpmovmskb    r8d, ymm4
        test         r8d, r8d
        jz           b2_scan
        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          b2_nully
        lea          rax, [r9 + r8]
        vzeroupper
        ret
b2_nully:
        xor          eax, eax
        vzeroupper
        ret
wia_b2 ENDP

; ============================================================ B3 : four 128-bit probes (64 bytes)
;                                                                  before any ymm is written
wia_b3 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           b3_p2
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          b3_null
        lea          rax, [r11 + r8]
        ret
b3_null:
        xor          eax, eax
        ret
b3_p2:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jnz          b3_hit
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jnz          b3_hit
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jnz          b3_hit
        jmp          b3_go256
b3_hit:
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          b3_null
        lea          rax, [r9 + r8]
        ret
b3_go256:
        add          r9, 16
        and          r9, -32
        vpbroadcastb ymm2, xmm2
        vpxor        ymm1, ymm1, ymm1
        sub          r9, 32
b3_scan:
        add          r9, 32
        vpcmpeqb     ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqb     ymm3, ymm1, ymmword ptr [r9]
        vpor         ymm4, ymm0, ymm3
        vpmovmskb    r8d, ymm4
        test         r8d, r8d
        jz           b3_scan
        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          b3_nully
        lea          rax, [r9 + r8]
        vzeroupper
        ret
b3_nully:
        xor          eax, eax
        vzeroupper
        ret
wia_b3 ENDP

END
