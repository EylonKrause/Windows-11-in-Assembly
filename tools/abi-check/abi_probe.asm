; tools/abi-check/abi_probe.asm
; unsigned long long wia_abi_probe(void (*thunk)(void))
;
; Calls thunk() with EVERY Win64 non-volatile register holding a distinct sentinel and returns a
; bitmask of the registers the callee failed to preserve. The thunk is ordinary compiled C that
; performs the real call with real arguments, so this one probe covers every signature -- two
; arguments or seven -- without knowing anything about them.
;
; WHY THIS EXISTS. Sixteen implementations in this repository quietly violated the Win64 ABI by
; using xmm6-xmm15 as scratch. Every one passed its correctness test, because a correctness test
; compares integers and strings, and a clobbered xmm6 only destroys a caller's live FLOATING-POINT
; state. The bug surfaced by pure accident: change 202's benchmark keeps its timing accumulators in
; xmm6/xmm7, so a perfectly correct function reported 0.00 ns. This probe removes the luck.
;
; WHAT THE ABI ACTUALLY SAYS, which is narrower than "never touch ymm6":
;   volatile      rax rcx rdx r8 r9 r10 r11, xmm0-xmm5, and the UPPER half of ymm0-ymm15
;   non-volatile  rbx rbp rdi rsi rsp r12-r15, and the LOW 128 BITS of xmm6-xmm15
; ymm6's high lane is free; its low lane is not. That is why the comparison below is 128 bits wide
; and not 256 -- a probe that checked the full ymm would report violations that are not violations.
;
; Bits 0-7 = rbx rbp rdi rsi r12 r13 r14 r15;  bits 8-17 = xmm6..xmm15;
; bit 18 = stack pointer not restored;  bit 19 = direction flag left set.

.const
ALIGN 16
xpat    dq  0C0DEBA5E00000006h, 0F00DFACE00000006h
        dq  0C0DEBA5E00000007h, 0F00DFACE00000007h
        dq  0C0DEBA5E00000008h, 0F00DFACE00000008h
        dq  0C0DEBA5E00000009h, 0F00DFACE00000009h
        dq  0C0DEBA5E0000000Ah, 0F00DFACE0000000Ah
        dq  0C0DEBA5E0000000Bh, 0F00DFACE0000000Bh
        dq  0C0DEBA5E0000000Ch, 0F00DFACE0000000Ch
        dq  0C0DEBA5E0000000Dh, 0F00DFACE0000000Dh
        dq  0C0DEBA5E0000000Eh, 0F00DFACE0000000Eh
        dq  0C0DEBA5E0000000Fh, 0F00DFACE0000000Fh
gpat    dq  0A5A5A5A5A5A50010h                    ; rbx
        dq  0A5A5A5A5A5A50011h                    ; rbp
        dq  0A5A5A5A5A5A50012h                    ; rdi
        dq  0A5A5A5A5A5A50013h                    ; rsi
        dq  0A5A5A5A5A5A50014h                    ; r12
        dq  0A5A5A5A5A5A50015h                    ; r13
        dq  0A5A5A5A5A5A50016h                    ; r14
        dq  0A5A5A5A5A5A50017h                    ; r15

.code
wia_abi_probe PROC
        push      rbx
        push      rbp
        push      rdi
        push      rsi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 0F8h                          ; keeps rsp 16-aligned at the call
        mov       r11, rcx                           ; the thunk
        mov       qword ptr [rsp + 38h], rsp         ; to prove the callee balanced the stack

        ; preserve the REAL xmm6-15 first: this probe must itself be ABI-clean.
        vmovdqu   xmmword ptr [rsp + 40h], xmm6
        vmovdqu   xmmword ptr [rsp + 50h], xmm7
        vmovdqu   xmmword ptr [rsp + 60h], xmm8
        vmovdqu   xmmword ptr [rsp + 70h], xmm9
        vmovdqu   xmmword ptr [rsp + 80h], xmm10
        vmovdqu   xmmword ptr [rsp + 90h], xmm11
        vmovdqu   xmmword ptr [rsp + 0A0h], xmm12
        vmovdqu   xmmword ptr [rsp + 0B0h], xmm13
        vmovdqu   xmmword ptr [rsp + 0C0h], xmm14
        vmovdqu   xmmword ptr [rsp + 0D0h], xmm15

        lea       r10, [xpat]                        ; lea first: an indexed symbol reference
                                                   ; would force an ADDR32 fixup (LNK2017)
        vmovdqa   xmm6  , xmmword ptr [r10 + 0]
        vmovdqa   xmm7  , xmmword ptr [r10 + 16]
        vmovdqa   xmm8  , xmmword ptr [r10 + 32]
        vmovdqa   xmm9  , xmmword ptr [r10 + 48]
        vmovdqa   xmm10 , xmmword ptr [r10 + 64]
        vmovdqa   xmm11 , xmmword ptr [r10 + 80]
        vmovdqa   xmm12 , xmmword ptr [r10 + 96]
        vmovdqa   xmm13 , xmmword ptr [r10 + 112]
        vmovdqa   xmm14 , xmmword ptr [r10 + 128]
        vmovdqa   xmm15 , xmmword ptr [r10 + 144]
        lea       r10, [gpat]
        mov       rbx , qword ptr [r10 + 0]
        mov       rbp , qword ptr [r10 + 8]
        mov       rdi , qword ptr [r10 + 16]
        mov       rsi , qword ptr [r10 + 24]
        mov       r12 , qword ptr [r10 + 32]
        mov       r13 , qword ptr [r10 + 40]
        mov       r14 , qword ptr [r10 + 48]
        mov       r15 , qword ptr [r10 + 56]

        call      r11

        xor       eax, eax
        lea       r11, [gpat]
        cmp       rbx , qword ptr [r11 + 0]
        je        g0
        or        eax, 1
g0:
        cmp       rbp , qword ptr [r11 + 8]
        je        g1
        or        eax, 2
g1:
        cmp       rdi , qword ptr [r11 + 16]
        je        g2
        or        eax, 4
g2:
        cmp       rsi , qword ptr [r11 + 24]
        je        g3
        or        eax, 8
g3:
        cmp       r12 , qword ptr [r11 + 32]
        je        g4
        or        eax, 16
g4:
        cmp       r13 , qword ptr [r11 + 40]
        je        g5
        or        eax, 32
g5:
        cmp       r14 , qword ptr [r11 + 48]
        je        g6
        or        eax, 64
g6:
        cmp       r15 , qword ptr [r11 + 56]
        je        g7
        or        eax, 128
g7:
        lea       r11, [xpat]
        vmovdqa   xmm0, xmmword ptr [r11 + 0]
        vpcmpeqb  xmm0, xmm0, xmm6
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x0
        or        eax, 256
x0:
        vmovdqa   xmm0, xmmword ptr [r11 + 16]
        vpcmpeqb  xmm0, xmm0, xmm7
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x1
        or        eax, 512
x1:
        vmovdqa   xmm0, xmmword ptr [r11 + 32]
        vpcmpeqb  xmm0, xmm0, xmm8
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x2
        or        eax, 1024
x2:
        vmovdqa   xmm0, xmmword ptr [r11 + 48]
        vpcmpeqb  xmm0, xmm0, xmm9
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x3
        or        eax, 2048
x3:
        vmovdqa   xmm0, xmmword ptr [r11 + 64]
        vpcmpeqb  xmm0, xmm0, xmm10
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x4
        or        eax, 4096
x4:
        vmovdqa   xmm0, xmmword ptr [r11 + 80]
        vpcmpeqb  xmm0, xmm0, xmm11
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x5
        or        eax, 8192
x5:
        vmovdqa   xmm0, xmmword ptr [r11 + 96]
        vpcmpeqb  xmm0, xmm0, xmm12
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x6
        or        eax, 16384
x6:
        vmovdqa   xmm0, xmmword ptr [r11 + 112]
        vpcmpeqb  xmm0, xmm0, xmm13
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x7
        or        eax, 32768
x7:
        vmovdqa   xmm0, xmmword ptr [r11 + 128]
        vpcmpeqb  xmm0, xmm0, xmm14
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x8
        or        eax, 65536
x8:
        vmovdqa   xmm0, xmmword ptr [r11 + 144]
        vpcmpeqb  xmm0, xmm0, xmm15
        vpmovmskb r10d, xmm0
        cmp       r10d, 0FFFFh                       ; low 128 bits only -- see header
        je        x9
        or        eax, 131072
x9:
        cmp       rsp, qword ptr [rsp + 38h]
        je        sp_ok
        or        eax, 262144
sp_ok:
        pushfq
        pop       r10
        test      r10d, 400h                         ; DF must be clear on return
        jz        df_ok
        or        eax, 524288
df_ok:
        vzeroupper
        vmovdqu   xmm6  , xmmword ptr [rsp + 40h]
        vmovdqu   xmm7  , xmmword ptr [rsp + 50h]
        vmovdqu   xmm8  , xmmword ptr [rsp + 60h]
        vmovdqu   xmm9  , xmmword ptr [rsp + 70h]
        vmovdqu   xmm10 , xmmword ptr [rsp + 80h]
        vmovdqu   xmm11 , xmmword ptr [rsp + 90h]
        vmovdqu   xmm12 , xmmword ptr [rsp + 0A0h]
        vmovdqu   xmm13 , xmmword ptr [rsp + 0B0h]
        vmovdqu   xmm14 , xmmword ptr [rsp + 0C0h]
        vmovdqu   xmm15 , xmmword ptr [rsp + 0D0h]
        add       rsp, 0F8h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rsi
        pop       rdi
        pop       rbp
        pop       rbx
        ret
wia_abi_probe ENDP
END
