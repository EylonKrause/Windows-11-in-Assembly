; v8 = v4 prologue + PEELED first digit + rotated steady-state loop (load at top, conditional back-edge)
.code
wia_ustr2int PROC
        movzx   eax, word ptr [rcx]
        mov     r9, [rcx+8]
        test    al, 1
        jnz     u_bad
        test    eax, eax
        jz      u_bad
        movzx   ecx, word ptr [r9]
        mov     r10, r9
        lea     r9, [r9+rax]
        sub     r10, r9
        xor     r11d, r11d
        cmp     ecx, 20h
        ja      u_sign
u_skip:
        add     r10, 2
        jz      u_nochar
        movzx   ecx, word ptr [r9+r10]
        cmp     ecx, 20h
        jbe     u_skip
u_sign:
        cmp     ecx, '+'
        je      u_signed
        cmp     ecx, '-'
        jne     u_base
        mov     r11d, 1
u_signed:
        add     r10, 2
        jz      u_nochar
        movzx   ecx, word ptr [r9+r10]
u_base:
        cmp     edx, 10
        je      u_d10
        test    edx, edx
        jz      u_auto
        cmp     edx, 16
        je      u_d16
        cmp     edx, 8
        je      u_d8
        cmp     edx, 2
        je      u_d2
u_bad:
        mov     dword ptr [r8], 0
        mov     eax, 0C000000Dh
        ret
u_nochar:
        xor     ecx, ecx
        jmp     u_base

u_auto: cmp     ecx, '0'
        jne     u_d10
        lea     rax, [r10+4]
        test    rax, rax
        jg      u_zero
        movzx   eax, word ptr [r9+r10+2]
        cmp     eax, 'x'
        je      u_px16
        cmp     eax, 'o'
        je      u_px8
        cmp     eax, 'b'
        je      u_px2
        add     r10, 2
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d10
u_px16: add     r10, 4
        jz      u_zero
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d16
u_px8:  add     r10, 4
        jz      u_zero
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d8
u_px2:  add     r10, 4
        jz      u_zero
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d2
u_zero: xor     eax, eax
        jmp     u_fin

; ---------------- DECIMAL ----------------
ALIGN 16
u_d10:  xor     eax, eax
        lea     edx, [rcx-30h]
        cmp     edx, 9
        ja      u_fin
        mov     eax, edx                  ; first digit: 0*10 + d
        add     r10, 2
        jz      u_fin
ALIGN 16
u_d10L: movzx   ecx, word ptr [r9+r10]
        lea     edx, [rcx-30h]
        cmp     edx, 9
        ja      u_fin
        lea     eax, [rax+rax*4]
        lea     eax, [rdx+rax*2]
        add     r10, 2
        jnz     u_d10L
u_fin:  test    r11d, r11d
        jz      u_store
        neg     eax
u_store:
        mov     [r8], eax
        xor     eax, eax
        ret

; ---------------- HEX ----------------
ALIGN 16
u_d16:  xor     eax, eax
        lea     edx, [rcx-30h]
        cmp     edx, 9
        jbe     u_d16P
        mov     edx, ecx
        or      edx, 20h
        sub     edx, 61h
        cmp     edx, 5
        ja      u_fin
        add     edx, 10
u_d16P: mov     eax, edx
        add     r10, 2
        jz      u_fin
ALIGN 16
u_d16L: movzx   ecx, word ptr [r9+r10]
        lea     edx, [rcx-30h]
        cmp     edx, 9
        jbe     u_d16A
        mov     edx, ecx
        or      edx, 20h
        sub     edx, 61h
        cmp     edx, 5
        ja      u_fin
        add     edx, 10
u_d16A: shl     eax, 4
        or      eax, edx
        add     r10, 2
        jnz     u_d16L
        jmp     u_fin

; ---------------- OCTAL ----------------
ALIGN 16
u_d8:   xor     eax, eax
        lea     edx, [rcx-30h]
        cmp     edx, 7
        ja      u_fin
        mov     eax, edx
        add     r10, 2
        jz      u_fin
ALIGN 16
u_d8L:  movzx   ecx, word ptr [r9+r10]
        lea     edx, [rcx-30h]
        cmp     edx, 7
        ja      u_fin
        shl     eax, 3
        or      eax, edx
        add     r10, 2
        jnz     u_d8L
        jmp     u_fin

; ---------------- BINARY ----------------
ALIGN 16
u_d2:   xor     eax, eax
        lea     edx, [rcx-30h]
        cmp     edx, 1
        ja      u_fin
        mov     eax, edx
        add     r10, 2
        jz      u_fin
ALIGN 16
u_d2L:  movzx   ecx, word ptr [r9+r10]
        lea     edx, [rcx-30h]
        cmp     edx, 1
        ja      u_fin
        lea     eax, [rdx+rax*2]
        add     r10, 2
        jnz     u_d2L
        jmp     u_fin

wia_ustr2int ENDP
END
