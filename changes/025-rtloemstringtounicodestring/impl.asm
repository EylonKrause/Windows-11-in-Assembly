; changes/025-rtloemstringtounicodestring/impl.asm
; NTSTATUS wia_oem2u(UNICODE_STRING* dst, const ANSI_STRING* src, BOOLEAN alloc)
;   [Win64: rcx, rdx, r8b -> eax]
;
; Reimplements the no-allocate path of ntdll!RtlOemStringToUnicodeString: widens
; single-byte ANSI src to UTF-16 dst->Buffer (one wchar per byte) and sets
; dst->Length = 2 * src->Length. All-ASCII 16-byte blocks widen with vpmovzxbw
; (16 -> 16 words); blocks with any byte >= 0x80 use wia_oem2umap[] (256-entry
; byte->wchar table from the OS codepage). alloc=TRUE -> STATUS_INVALID_PARAMETER.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_oem2umap:WORD

.code
wia_oem2u PROC
        test      r8b, r8b
        jnz       alloc_unsupported
        movzx     eax, word ptr [rdx]              ; src Length (bytes) = n
        mov       r11, [rdx + 8]                   ; srcBuf
        mov       r10, [rcx + 8]                   ; dstBuf
        lea       edx, [eax*2]                     ; out bytes = 2n
        movzx     r9d, word ptr [rcx + 2]          ; dst MaximumLength
        ; The export nul-terminates, so it needs room for 2n bytes of conversion plus a two-byte
        ; terminator. This used to compare 2n against MaximumLength, room for the conversion
        ; alone, and the success path wrote no terminator. Both halves were wrong together, which
        ; is why the change's own gate saw neither: MaximumLength was fixed and generous in every
        ; case, so the size rule never bound, and the comparison stopped at Length, so the wchar at
        ; [Length/2] was outside it by construction.
        ;
        ; Measured: a 4-character source needs MaximumLength >= 10, and 9 is not enough, the
        ; rule is on the byte count, so an ODD MaximumLength of 11 succeeds where 9 fails. On
        ; overflow this one writes nothing and leaves dst->Length alone, unlike sibling 018 which
        ; truncates and partially writes. Found by live substitution; see live_subst_ntconv2.c.
        lea       r8d, [rdx + 2]                    ; 2n + 2
        cmp       r8d, r9d
        ja        overflow
        mov       word ptr [rcx], dx               ; dst->Length = 2n
        mov       r8d, eax                          ; n (byte count = wchar count)
        xor       r9d, r9d                          ; i
        lea       rcx, wia_oem2umap                  ; table
w_loop:
        mov       edx, r8d
        sub       edx, r9d
        cmp       edx, 16
        jb        w_tail
        vmovdqu   xmm0, xmmword ptr [r11 + r9]
        vpmovmskb eax, xmm0
        test      eax, eax
        jnz       w_sblock
        vpmovzxbw ymm0, xmm0
        vmovdqu   ymmword ptr [r10 + r9*2], ymm0
        add       r9, 16
        jmp       w_loop
w_sblock:
        mov       edx, 16
w_sb:
        movzx     eax, byte ptr [r11 + r9]
        movzx     eax, word ptr [rcx + rax*2]
        mov       word ptr [r10 + r9*2], ax
        add       r9, 1
        dec       edx
        jnz       w_sb
        jmp       w_loop
w_tail:
        cmp       r9d, r8d
        jae       w_done
        movzx     eax, byte ptr [r11 + r9]
        movzx     eax, word ptr [rcx + rax*2]
        mov       word ptr [r10 + r9*2], ax
        add       r9, 1
        jmp       w_tail
w_done:
        mov       word ptr [r10 + r8*2], 0          ; the terminator, one WCHAR at [Length/2]
        xor       eax, eax
        vzeroupper
        ret
overflow:
        mov       eax, 80000005h
        ret
alloc_unsupported:
        mov       eax, 0C000000Dh
        ret
wia_oem2u ENDP
END
