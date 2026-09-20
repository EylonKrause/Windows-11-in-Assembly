; changes/021-rtlunicodetomultibyten/impl.asm
; NTSTATUS wia_u2mb(char* dst, ulong maxBytes, pulong outLen, const wchar_t* src, ulong srcBytes)
;   [Win64: rcx, edx, r8, r9, [rsp+0x28] -> eax; *outLen = bytes written]
;
; Reimplements ntdll!RtlUnicodeToMultiByteN (UTF-16 -> single-byte ANSI codepage,
; 1 byte per wchar). Writes up to maxBytes; *outLen = bytes written. All-ASCII
; 16-wchar blocks pack 16 -> 16 bytes (vpackuswb + vpermq); other wchars via the
; 65536-entry wia_ansimap[] byte table from the OS codepage.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_ansimap:BYTE

.const
ALIGN 16
CFF80   DW      16 dup(0FF80h)

.code
wia_u2mb PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        mov       rbx, rcx                          ; dst
        mov       edi, edx                          ; maxBytes
        mov       r12, r8                           ; outLen ptr
        mov       rsi, r9                           ; src
        mov       r13d, dword ptr [rsp + 50h]       ; srcBytes
        shr       r13d, 1                           ; n wchars
        xor       r9d, r9d                          ; i (== bytes written)
        lea       rcx, wia_ansimap
mb_loop:
        cmp       r9d, r13d
        jae       mb_done
        cmp       r9d, edi
        jae       mb_done                           ; output full
        ; ASCII fast path: 16 wchars, room for 16 bytes, all < 0x80
        mov       r8d, r13d
        sub       r8d, r9d
        cmp       r8d, 16
        jb        mb_ascii8
        mov       r8d, edi
        sub       r8d, r9d
        cmp       r8d, 16
        jb        mb_ascii8
        vmovdqu   ymm0, ymmword ptr [rsi + r9*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80]
        vptest    ymm1, ymm1
        jnz       mb_scalar
        vpackuswb ymm0, ymm0, ymm0
        vpermq    ymm0, ymm0, 0D8h
        vmovdqu   xmmword ptr [rbx + r9], xmm0
        add       r9, 16
        jmp       mb_loop
mb_ascii8:
        mov       r8d, r13d
        sub       r8d, r9d
        cmp       r8d, 8
        jb        mb_scalar
        mov       r8d, edi
        sub       r8d, r9d
        cmp       r8d, 8
        jb        mb_scalar
        vmovdqu   xmm0, xmmword ptr [rsi + r9*2]
        vpand     xmm1, xmm0, xmmword ptr [CFF80]
        vptest    xmm1, xmm1
        jnz       mb_scalar
        vpackuswb xmm0, xmm0, xmm0
        vmovq     qword ptr [rbx + r9], xmm0
        add       r9, 8
        jmp       mb_loop
mb_scalar:
        movzx     eax, word ptr [rsi + r9*2]
        movzx     eax, byte ptr [rcx + rax]
        mov       byte ptr [rbx + r9], al
        add       r9, 1
        jmp       mb_loop
mb_done:
        mov       dword ptr [r12], r9d              ; *outLen = bytes written
        xor       eax, eax                          ; STATUS_SUCCESS (ntdll does not report overflow here)
        vzeroupper
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_u2mb ENDP
END
