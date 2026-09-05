; ntdll.dll!RtlUpcaseUnicodeString  --  hand-written x86-64 reimplementation (9.12x vs shipped)
; source of truth: changes/015-rtlupcaseunicodestring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/015-rtlupcaseunicodestring/impl.asm
; NTSTATUS wia_upcasestr(UNICODE_STRING* dst, const UNICODE_STRING* src, BOOLEAN alloc)
;   [Win64: rcx, rdx, r8b -> eax]
;
; Reimplements the no-allocate path of ntdll!RtlUpcaseUnicodeString: upcases src
; into the caller-provided dst->Buffer and sets dst->Length. All-ASCII 16-wchar
; blocks upcase a-z in-register (range subtract); blocks with any wchar >= 0x80
; use the wia_upcase[] table (built from the OS, so bit-exact all-Unicode).
; alloc=TRUE (heap allocation) is out of scope and returns STATUS_INVALID_PARAMETER.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_upcase:WORD

.const
ALIGN 16
C0060   DW      16 dup(0060h)
C007A   DW      16 dup(007Ah)
C0020   DW      16 dup(0020h)
CFF80   DW      16 dup(0FF80h)

.code
wia_upcasestr PROC
        test      r8b, r8b
        jnz       alloc_unsupported
        movzx     eax, word ptr [rdx]              ; src Length (bytes)
        movzx     r9d, word ptr [rcx + 2]          ; dst MaximumLength
        cmp       eax, r9d
        ja        overflow
        mov       word ptr [rcx], ax               ; dst->Length = src->Length
        mov       r11, [rdx + 8]                   ; srcBuf
        mov       r10, [rcx + 8]                   ; dstBuf
        mov       r8d, eax                          ; n (bytes)
        xor       r9d, r9d                          ; offset
        lea       rcx, wia_upcase                  ; table
up_loop:
        mov       edx, r8d
        sub       edx, r9d
        cmp       edx, 32
        jb        up_tail
        vmovdqu   ymm0, ymmword ptr [r11 + r9]
        vpand     ymm1, ymm0, ymmword ptr [CFF80]
        vptest    ymm1, ymm1
        jnz       up_sblock
        vpcmpgtw  ymm1, ymm0, ymmword ptr [C0060]
        vpcmpgtw  ymm2, ymm0, ymmword ptr [C007A]
        vpandn    ymm1, ymm2, ymm1
        vpand     ymm1, ymm1, ymmword ptr [C0020]
        vpsubw    ymm0, ymm0, ymm1
        vmovdqu   ymmword ptr [r10 + r9], ymm0
        add       r9, 32
        jmp       up_loop
up_sblock:
        mov       edx, 16
up_sb:
        movzx     eax, word ptr [r11 + r9]
        movzx     eax, word ptr [rcx + rax*2]
        mov       word ptr [r10 + r9], ax
        add       r9, 2
        dec       edx
        jnz       up_sb
        jmp       up_loop
up_tail:
        cmp       r9d, r8d
        jae       up_done
        movzx     eax, word ptr [r11 + r9]
        movzx     eax, word ptr [rcx + rax*2]
        mov       word ptr [r10 + r9], ax
        add       r9, 2
        jmp       up_tail
up_done:
        xor       eax, eax                          ; STATUS_SUCCESS
        vzeroupper
        ret
overflow:
        mov       eax, 80000005h                    ; STATUS_BUFFER_OVERFLOW
        ret
alloc_unsupported:
        mov       eax, 0C000000Dh                   ; STATUS_INVALID_PARAMETER
        ret
wia_upcasestr ENDP
END
