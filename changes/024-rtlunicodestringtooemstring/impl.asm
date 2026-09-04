; changes/024-rtlunicodestringtooemstring/impl.asm
; NTSTATUS wia_u2oem(ANSI_STRING* dst, const UNICODE_STRING* src, BOOLEAN alloc)
;   [Win64: rcx, rdx, r8b -> eax]
;
; Reimplements the no-allocate path of ntdll!RtlUnicodeStringToOemString:
; narrows UTF-16 src to single-byte ANSI dst->Buffer (one byte per wchar) and sets
; dst->Length. All-ASCII 16-wchar blocks pack 16 -> 16 bytes (vpackuswb + vpermq);
; blocks with any wchar >= 0x80 use wia_oemmap[] (built from the OS codepage, so
; bit-exact). alloc=TRUE returns STATUS_INVALID_PARAMETER.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_oemmap:BYTE

.const
ALIGN 16
CFF80   DW      16 dup(0FF80h)

.code
wia_u2oem PROC
        test      r8b, r8b
        jnz       alloc_unsupported
        movzx     eax, word ptr [rdx]              ; src Length (bytes)
        shr       eax, 1                            ; n = out bytes (wchar count)
        movzx     r9d, word ptr [rcx + 2]          ; dst MaximumLength
        cmp       eax, r9d
        ja        overflow
        mov       word ptr [rcx], ax               ; dst->Length = n
        mov       r11, [rdx + 8]                   ; srcBuf
        mov       r10, [rcx + 8]                   ; dstBuf
        mov       r8d, eax                          ; n
        xor       r9d, r9d                          ; i
        lea       rcx, wia_oemmap                 ; table
a_loop:
        mov       edx, r8d
        sub       edx, r9d
        cmp       edx, 16
        jb        a_tail
        vmovdqu   ymm0, ymmword ptr [r11 + r9*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80]
        vptest    ymm1, ymm1
        jnz       a_sblock
        vpackuswb ymm0, ymm0, ymm0
        vpermq    ymm0, ymm0, 0D8h
        vmovdqu   xmmword ptr [r10 + r9], xmm0
        add       r9, 16
        jmp       a_loop
a_sblock:
        mov       edx, 16
a_sb:
        movzx     eax, word ptr [r11 + r9*2]
        movzx     eax, byte ptr [rcx + rax]
        mov       byte ptr [r10 + r9], al
        add       r9, 1
        dec       edx
        jnz       a_sb
        jmp       a_loop
a_tail:
        cmp       r9d, r8d
        jae       a_done
        movzx     eax, word ptr [r11 + r9*2]
        movzx     eax, byte ptr [rcx + rax]
        mov       byte ptr [r10 + r9], al
        add       r9, 1
        jmp       a_tail
a_done:
        xor       eax, eax
        vzeroupper
        ret
overflow:
        mov       eax, 80000005h
        ret
alloc_unsupported:
        mov       eax, 0C000000Dh
        ret
wia_u2oem ENDP
END
