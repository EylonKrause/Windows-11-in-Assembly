; changes/294-strchr/probes/blockcount.asm: GENERATED, do not hand-edit (gen_blocks.py).
;
; How many 128-BIT blocks before going wide, and which wide loop, measured, not argued.
;
; The four-block prologue in the first landed draft covers [base, base+64), where base = s & -16.
; When the caller's string starts at the END of its 16-byte block (s & 15 == 15) that is only 49
; bytes of STRING, so a 55..80-byte string spills past it and pays the whole wide-path entry for a
; handful of remaining bytes. probes/smallband.c measured that at 0.79x against the live export --
; a regression bench.c could not see, because bench.c's rows come from malloc and malloc never
; handed it a badly-aligned start.
;
; Each variant below is impl.asm with only the block count and the wide loop changed:
;   n4   4 blocks, 512-bit loop   (the first draft)
;   n5   5 blocks, 512-bit loop
;   n6   6 blocks, 512-bit loop
;   n8   8 blocks, 512-bit loop
;   n6y  6 blocks, 256-bit loop   (separates "how many blocks" from "which wide loop")
;   n8y  8 blocks, 256-bit loop
;
; The `and -64` that reaches the wide loop needs the prologue to end at base+64 or beyond, which
; every variant here does; see impl.asm note 5 for why.

.code

wia_n4 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        xor          r10d, r10d

        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           n4_b2
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r11 + r8]
        cmovnc       rax, r10
        ret
n4_b2:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n4_b3
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n4_b3:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n4_b4
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n4_b4:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n4_wide
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret

n4_wide:
        lea          r9, [r9 + 16]
        and          r9, -64
        vpbroadcastb zmm16, xmm2
        sub          r9, 64
n4_scan:
        add          r9, 64
        vmovdqa64    zmm17, zmmword ptr [r9]
        vpcmpeqb     k1, zmm17, zmm16
        vptestnmb    k2, zmm17, zmm17
        kortestq     k1, k2
        jz           n4_scan
        korq         k3, k1, k2
        kmovq        rax, k3
        tzcnt        rax, rax
        kmovq        rdx, k1
        bt           rdx, rax
        lea          rax, [r9 + rax]
        cmovnc       rax, r10
        vzeroupper
        ret

wia_n4 ENDP

wia_n5 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        xor          r10d, r10d

        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           n5_b2
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r11 + r8]
        cmovnc       rax, r10
        ret
n5_b2:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n5_b3
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n5_b3:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n5_b4
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n5_b4:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n5_b5
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n5_b5:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n5_wide
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret

n5_wide:
        lea          r9, [r9 + 16]
        and          r9, -64
        vpbroadcastb zmm16, xmm2
        sub          r9, 64
n5_scan:
        add          r9, 64
        vmovdqa64    zmm17, zmmword ptr [r9]
        vpcmpeqb     k1, zmm17, zmm16
        vptestnmb    k2, zmm17, zmm17
        kortestq     k1, k2
        jz           n5_scan
        korq         k3, k1, k2
        kmovq        rax, k3
        tzcnt        rax, rax
        kmovq        rdx, k1
        bt           rdx, rax
        lea          rax, [r9 + rax]
        cmovnc       rax, r10
        vzeroupper
        ret

wia_n5 ENDP

wia_n6 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        xor          r10d, r10d

        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           n6_b2
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r11 + r8]
        cmovnc       rax, r10
        ret
n6_b2:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6_b3
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n6_b3:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6_b4
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n6_b4:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6_b5
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n6_b5:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6_b6
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n6_b6:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6_wide
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret

n6_wide:
        lea          r9, [r9 + 16]
        and          r9, -64
        vpbroadcastb zmm16, xmm2
        sub          r9, 64
n6_scan:
        add          r9, 64
        vmovdqa64    zmm17, zmmword ptr [r9]
        vpcmpeqb     k1, zmm17, zmm16
        vptestnmb    k2, zmm17, zmm17
        kortestq     k1, k2
        jz           n6_scan
        korq         k3, k1, k2
        kmovq        rax, k3
        tzcnt        rax, rax
        kmovq        rdx, k1
        bt           rdx, rax
        lea          rax, [r9 + rax]
        cmovnc       rax, r10
        vzeroupper
        ret

wia_n6 ENDP

wia_n8 PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        xor          r10d, r10d

        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           n8_b2
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r11 + r8]
        cmovnc       rax, r10
        ret
n8_b2:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8_b3
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8_b3:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8_b4
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8_b4:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8_b5
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8_b5:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8_b6
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8_b6:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8_b7
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8_b7:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8_b8
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8_b8:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8_wide
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret

n8_wide:
        lea          r9, [r9 + 16]
        and          r9, -64
        vpbroadcastb zmm16, xmm2
        sub          r9, 64
n8_scan:
        add          r9, 64
        vmovdqa64    zmm17, zmmword ptr [r9]
        vpcmpeqb     k1, zmm17, zmm16
        vptestnmb    k2, zmm17, zmm17
        kortestq     k1, k2
        jz           n8_scan
        korq         k3, k1, k2
        kmovq        rax, k3
        tzcnt        rax, rax
        kmovq        rdx, k1
        bt           rdx, rax
        lea          rax, [r9 + rax]
        cmovnc       rax, r10
        vzeroupper
        ret

wia_n8 ENDP

wia_n6y PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        xor          r10d, r10d

        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           n6y_b2
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r11 + r8]
        cmovnc       rax, r10
        ret
n6y_b2:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6y_b3
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n6y_b3:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6y_b4
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n6y_b4:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6y_b5
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n6y_b5:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6y_b6
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n6y_b6:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n6y_wide
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret

n6y_wide:
        lea          r9, [r9 + 16]
        and          r9, -64
        vpbroadcastb ymm2, xmm2
        vpxor        ymm1, ymm1, ymm1
        sub          r9, 32
n6y_scan:
        add          r9, 32
        vpcmpeqb     ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqb     ymm3, ymm1, ymmword ptr [r9]
        vpor         ymm4, ymm0, ymm3
        vpmovmskb    r8d, ymm4
        test         r8d, r8d
        jz           n6y_scan
        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        vzeroupper
        ret

wia_n6y ENDP

wia_n8y PROC
        movzx        eax, dl
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx
        mov          r9, rcx
        and          r9, -16
        mov          ecx, r11d
        and          ecx, 15
        xor          r10d, r10d

        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl
        shr          edx, cl
        test         r8d, r8d
        jz           n8y_b2
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r11 + r8]
        cmovnc       rax, r10
        ret
n8y_b2:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8y_b3
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8y_b3:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8y_b4
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8y_b4:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8y_b5
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8y_b5:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8y_b6
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8y_b6:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8y_b7
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8y_b7:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8y_b8
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret
n8y_b8:
        add          r9, 16
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           n8y_wide
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        ret

n8y_wide:
        lea          r9, [r9 + 16]
        and          r9, -64
        vpbroadcastb ymm2, xmm2
        vpxor        ymm1, ymm1, ymm1
        sub          r9, 32
n8y_scan:
        add          r9, 32
        vpcmpeqb     ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqb     ymm3, ymm1, ymmword ptr [r9]
        vpor         ymm4, ymm0, ymm3
        vpmovmskb    r8d, ymm4
        test         r8d, r8d
        jz           n8y_scan
        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        lea          rax, [r9 + r8]
        cmovnc       rax, r10
        vzeroupper
        ret

wia_n8y ENDP

END
