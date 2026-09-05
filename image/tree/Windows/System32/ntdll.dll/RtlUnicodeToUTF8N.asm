; ntdll.dll!RtlUnicodeToUTF8N  --  hand-written x86-64 reimplementation (2.82x vs shipped)
; source of truth: changes/016-rtlunicodetoutf8n/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/016-rtlunicodetoutf8n/impl.asm
; NTSTATUS wia_u2u8(void* dst, ULONG dstMax, PULONG outLen, const wchar_t* src, ULONG srcBytes)
;   [Win64: rcx, edx, r8, r9, [rsp+0x28] -> eax; *outLen = bytes produced]
;
; Reimplements ntdll!RtlUnicodeToUTF8N (UTF-16 -> UTF-8). Validated in C against
; the live ntdll oracle (0 mismatches / 60000, incl. surrogate pairs and lone
; surrogates -> U+FFFD). ASCII fast path packs 8 wchars -> 8 bytes; everything
; else (2/3/4-byte sequences, surrogate decode) is exact scalar.
; Status: STATUS_SUCCESS, or 0x107 STATUS_SOME_NOT_MAPPED (a lone surrogate), or
; 0xC0000023 STATUS_BUFFER_TOO_SMALL (output did not fit).
;
; ISA: AVX2. Validated on Zen3.

.const
ALIGN 16
CFF80x  DW      8 dup(0FF80h)
CFF80y  DW      16 dup(0FF80h)

.code
wia_u2u8 PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rbx, rcx                          ; dst
        mov       edi, edx                          ; dstMax (zero-extended)
        mov       r12, r8                           ; outLen ptr
        mov       rsi, r9                           ; src
        mov       r13d, dword ptr [rsp + 60h]       ; srcBytes (5th arg)
        shr       r13d, 1                           ; srcN wchars
        sub       rsp, 16
        mov       dword ptr [rsp], 0                ; [rsp+0] = someNotMapped
        mov       dword ptr [rsp+4], 0              ; [rsp+4] = overflow
        xor       r14, r14                          ; srcIdx
        xor       r15, r15                          ; dstPos

mainloop:
        cmp       r14d, r13d
        jae       done
        ; ---- ASCII fast path: 16 wchars all < 0x80, with room ----
        mov       r8d, r13d
        sub       r8d, r14d
        cmp       r8d, 16
        jb        ascii8
        lea       r8, [r15 + 16]
        cmp       r8, rdi
        ja        ascii8
        vmovdqu   ymm0, ymmword ptr [rsi + r14*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80y]
        vptest    ymm1, ymm1
        jnz       ascii8
        vpackuswb ymm0, ymm0, ymm0
        vpermq    ymm0, ymm0, 0D8h                  ; gather packed bytes into low 16
        vmovdqu   xmmword ptr [rbx + r15], xmm0
        add       r15, 16
        add       r14, 16
        jmp       mainloop
ascii8:
        ; ---- ASCII fast path: 8 wchars all < 0x80, with room ----
        mov       r8d, r13d
        sub       r8d, r14d
        cmp       r8d, 8
        jb        scalar_char
        lea       r8, [r15 + 8]
        cmp       r8, rdi
        ja        scalar_char
        vmovdqu   xmm0, xmmword ptr [rsi + r14*2]
        vpand     xmm1, xmm0, xmmword ptr [CFF80x]
        vptest    xmm1, xmm1
        jnz       scalar_char
        vpackuswb xmm0, xmm0, xmm0
        vmovq     qword ptr [rbx + r15], xmm0
        add       r15, 8
        add       r14, 8
        jmp       mainloop

scalar_char:
        movzx     eax, word ptr [rsi + r14*2]       ; c
        cmp       eax, 80h
        jb        emit1
        cmp       eax, 800h
        jb        emit2
        cmp       eax, 0D800h
        jb        emit3
        cmp       eax, 0DC00h
        jb        high_surr
        cmp       eax, 0E000h
        jb        low_lone
        jmp       emit3

emit1:
        lea       r8, [r15 + 1]
        cmp       r8, rdi
        jbe       e1w
        mov       dword ptr [rsp+4], 1
        jmp       done
e1w:    mov       byte ptr [rbx + r15], al
e1a:    inc       r15
        inc       r14
        jmp       mainloop

emit2:
        lea       r8, [r15 + 2]
        cmp       r8, rdi
        jbe       e2w
        mov       dword ptr [rsp+4], 1
        jmp       done
e2w:    mov       r9d, eax
        shr       r9d, 6
        or        r9d, 0C0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
e2a:    add       r15, 2
        inc       r14
        jmp       mainloop

emit3:
        lea       r8, [r15 + 3]
        cmp       r8, rdi
        jbe       e3w
        mov       dword ptr [rsp+4], 1
        jmp       done
e3w:    mov       r9d, eax
        shr       r9d, 12
        or        r9d, 0E0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        shr       r9d, 6
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 2], r9b
e3a:    add       r15, 3
        inc       r14
        jmp       mainloop

high_surr:
        lea       r8d, [r14 + 1]
        cmp       r8d, r13d
        jae       lone
        movzx     r9d, word ptr [rsi + r8*2]        ; next
        cmp       r9d, 0DC00h
        jb        lone
        cmp       r9d, 0E000h
        jae       lone
        ; valid pair -> codepoint in eax
        sub       eax, 0D800h
        shl       eax, 10
        sub       r9d, 0DC00h
        add       eax, r9d
        add       eax, 10000h
        lea       r8, [r15 + 4]
        cmp       r8, rdi
        jbe       e4w
        mov       dword ptr [rsp+4], 1
        jmp       done
e4w:    mov       r9d, eax
        shr       r9d, 18
        or        r9d, 0F0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        shr       r9d, 12
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
        mov       r9d, eax
        shr       r9d, 6
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 2], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 3], r9b
e4a:    add       r15, 4
        add       r14, 2                             ; consumed 2 wchars
        jmp       mainloop

low_lone:
lone:
        mov       dword ptr [rsp], 1                 ; someNotMapped
        lea       r8, [r15 + 3]
        cmp       r8, rdi
        jbe       ffw
        mov       dword ptr [rsp+4], 1
        jmp       done
ffw:    mov       byte ptr [rbx + r15], 0EFh
        mov       byte ptr [rbx + r15 + 1], 0BFh
        mov       byte ptr [rbx + r15 + 2], 0BDh
ffa:    add       r15, 3
        inc       r14
        jmp       mainloop

done:
        mov       dword ptr [r12], r15d             ; *outLen = dstPos
        mov       eax, dword ptr [rsp+4]
        test      eax, eax
        jnz       ret_small
        mov       eax, dword ptr [rsp]
        test      eax, eax
        jnz       ret_nm
        xor       eax, eax
        jmp       epi
ret_small:
        mov       eax, 0C0000023h
        jmp       epi
ret_nm:
        mov       eax, 107h
epi:
        add       rsp, 16
        vzeroupper
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_u2u8 ENDP
END
