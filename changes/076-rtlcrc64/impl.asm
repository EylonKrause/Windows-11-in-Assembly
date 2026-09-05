; changes/076-rtlcrc64/impl.asm
; unsigned __int64 wia_crc64(const void* Src, SIZE_T Len, unsigned __int64 Init)
;   [Win64: rcx=Src, rdx=Len, r8=Init -> rax]
;
; Reimplements ntdll!RtlCrc64 (reverse-engineered): reflected CRC-64, poly
; 0x9A6C9329AC4BC9B5, internal accumulator = ~Init, output = ~crc.
;   * Len < 128: slicing-by-8 (8 bytes/iter, 8-table lookup) + byte tail -- beats ntdll's
;     slicing at small sizes (its per-call overhead is heavy: ~0.9 GB/s at 32 B).
;   * Len >= 128: VPCLMULQDQ fold. Reflected-fold constants (derived + validated vs the live
;     export over 300k inputs): fold a 128-bit block by advancing 128 bits with
;     X' = clmul(X.lo, KA) ^ clmul(X.hi, KB) ^ next16, KA = x^128 = 0xEADC41FD2BA3D420,
;     KB = x^64 = 0x21E9761E252621AC. Final reduce of the 128-bit accumulator is
;     crc = SLICE8(SLICE8(X.lo)) ^ SLICE8(X.hi), where SLICE8(v) = mulx^64(v) is one
;     slicing-by-8 step over the SAME tables (mulx^64(v) == icrc(v as 8 bytes, 0)).
; wia_crc64_tab (slicing-by-8 tables) built once by wia_crc64_init.
; ISA: PCLMULQDQ + AVX + BMI2. Validated bit-exact vs live ntdll on Zen3.

EXTERN wia_crc64_tab:QWORD

.const
ALIGN 16
foldK   dq 0EADC41FD2BA3D420h, 021E9761E252621ACh   ; xmm: lo=KA(x^128), hi=KB(x^64)

.code

; SLICE8: OUT = T7[IN&FF]^T6[(IN>>8)&FF]^...^T0[(IN>>56)&FF]   (r11=table base, r8=scratch;
; IN preserved, IN/OUT must not be r8)
SLICE8  MACRO IN, OUT
        mov       r8, IN
        movzx     r8d, r8b
        mov       OUT, qword ptr [r11 + 3800h + r8*8]
        rorx      r8, IN, 8
        movzx     r8d, r8b
        xor       OUT, qword ptr [r11 + 3000h + r8*8]
        rorx      r8, IN, 16
        movzx     r8d, r8b
        xor       OUT, qword ptr [r11 + 2800h + r8*8]
        rorx      r8, IN, 24
        movzx     r8d, r8b
        xor       OUT, qword ptr [r11 + 2000h + r8*8]
        rorx      r8, IN, 32
        movzx     r8d, r8b
        xor       OUT, qword ptr [r11 + 1800h + r8*8]
        rorx      r8, IN, 40
        movzx     r8d, r8b
        xor       OUT, qword ptr [r11 + 1000h + r8*8]
        rorx      r8, IN, 48
        movzx     r8d, r8b
        xor       OUT, qword ptr [r11 + 0800h + r8*8]
        rorx      r8, IN, 56
        movzx     r8d, r8b
        xor       OUT, qword ptr [r11 + r8*8]
        ENDM

wia_crc64 PROC
        mov       r9, r8
        not       r9                                 ; crc = ~Init
        lea       r11, wia_crc64_tab
        cmp       rdx, 128
        jb        small

        ; ================= PCLMUL fold (Len >= 128) =================
        ; head: process (Len & 15) bytes via T0 so the rest is a multiple of 16
        mov       rax, rdx
        and       rax, 15
        sub       rdx, rax                           ; rdx = Len - head (multiple of 16)
hhl:
        test      rax, rax
        jz        hdone
        movzx     r8d, byte ptr [rcx]
        xor       r8b, r9b
        movzx     r8d, r8b
        shr       r9, 8
        xor       r9, qword ptr [r11 + r8*8]
        inc       rcx
        dec       rax
        jmp       hhl
hdone:
        vmovq     xmm0, r9                            ; crc in low 64
        vpxor     xmm0, xmm0, xmmword ptr [rcx]       ; X0 = first16 ^ crc
        add       rcx, 16
        sub       rdx, 16
        vmovdqa   xmm2, xmmword ptr [foldK]
floop:
        cmp       rdx, 16
        jb        freduce
        vpclmulqdq xmm3, xmm0, xmm2, 00h              ; X.lo * KA
        vpclmulqdq xmm4, xmm0, xmm2, 11h              ; X.hi * KB
        vpxor     xmm0, xmm3, xmm4
        vpxor     xmm0, xmm0, xmmword ptr [rcx]       ; ^ next block
        add       rcx, 16
        sub       rdx, 16
        jmp       floop
freduce:
        vmovq     r9, xmm0                            ; X.lo
        vpextrq   r10, xmm0, 1                        ; X.hi
        SLICE8    r9, rax                             ; rax = mulx^64(X.lo)
        SLICE8    rax, r9                             ; r9  = mulx^128(X.lo)
        SLICE8    r10, rax                            ; rax = mulx^64(X.hi)
        xor       r9, rax                             ; crc = mulx^128(X.lo) ^ mulx^64(X.hi)
        mov       rax, r9
        not       rax
        ret

        ; ================= slicing-by-8 (Len < 128) =================
small:
        cmp       rdx, 8
        jb        tail
main:
        xor       r9, qword ptr [rcx]                ; crc ^= 8 bytes
        movzx     r10d, r9b
        mov       rax, qword ptr [r11 + 3800h + r10*8]
        rorx      r10, r9, 8
        movzx     r10d, r10b
        mov       r8,  qword ptr [r11 + 3000h + r10*8]
        rorx      r10, r9, 16
        movzx     r10d, r10b
        xor       rax, qword ptr [r11 + 2800h + r10*8]
        rorx      r10, r9, 24
        movzx     r10d, r10b
        xor       r8,  qword ptr [r11 + 2000h + r10*8]
        rorx      r10, r9, 32
        movzx     r10d, r10b
        xor       rax, qword ptr [r11 + 1800h + r10*8]
        rorx      r10, r9, 40
        movzx     r10d, r10b
        xor       r8,  qword ptr [r11 + 1000h + r10*8]
        rorx      r10, r9, 48
        movzx     r10d, r10b
        xor       rax, qword ptr [r11 + 0800h + r10*8]
        rorx      r10, r9, 56
        movzx     r10d, r10b
        xor       r8,  qword ptr [r11 + r10*8]
        xor       rax, r8
        mov       r9, rax
        add       rcx, 8
        sub       rdx, 8
        cmp       rdx, 8
        jae       main
tail:
        test      rdx, rdx
        jz        done
        movzx     eax, byte ptr [rcx]
        xor       al, r9b
        movzx     eax, al
        shr       r9, 8
        xor       r9, qword ptr [r11 + rax*8]
        inc       rcx
        dec       rdx
        jmp       tail
done:
        mov       rax, r9
        not       rax
        ret
wia_crc64 ENDP
END
