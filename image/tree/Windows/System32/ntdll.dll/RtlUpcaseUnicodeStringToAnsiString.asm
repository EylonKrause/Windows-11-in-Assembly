; ntdll.dll!RtlUpcaseUnicodeStringToAnsiString  --  hand-written x86-64 reimplementation (9.0x vs shipped)
; source of truth: changes/020-rtlupcaseunicodestringtoansistring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/020-rtlupcaseunicodestringtoansistring/impl.asm
; NTSTATUS wia_u2au(ANSI_STRING* dst, const UNICODE_STRING* src, BOOLEAN alloc)
;   [Win64: rcx, rdx, r8b -> eax]
;
; Reimplements the no-allocate path of ntdll!RtlUpcaseUnicodeStringToAnsiString:
; upcases then narrows UTF-16 src to single-byte ANSI dst->Buffer (one byte per wchar) and sets
; dst->Length. All-ASCII 16-wchar blocks pack 16 -> 16 bytes (vpackuswb + vpermq);
; blocks with any wchar >= 0x80 use wia_ansimap[] (built from the OS codepage, so
; bit-exact). alloc=TRUE returns STATUS_INVALID_PARAMETER.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_upansimap:BYTE

.const
ALIGN 16
CFF80   DW      16 dup(0FF80h)
C0060   DW      16 dup(0060h)
C007A   DW      16 dup(007Ah)
C0020   DW      16 dup(0020h)

.code
wia_u2au PROC
        test      r8b, r8b
        jnz       alloc_unsupported
        movzx     eax, word ptr [rdx]              ; src Length (bytes)
        shr       eax, 1                            ; n = out bytes (wchar count)
        movzx     r9d, word ptr [rcx + 2]          ; dst MaximumLength
        ; The export nul-terminates, so it needs room for the conversion plus one byte. This used
        ; to be `cmp eax, r9d / ja overflow` -- room for the conversion alone -- and the success
        ; path wrote no terminator. Both halves were wrong together, which is exactly why the
        ; change's own gate saw neither: it ran every case with MaximumLength fixed and generous,
        ; so the size rule never bound, and compared only indices 0..Length-1, so the byte at
        ; [Length] was outside the comparison by construction.
        ;
        ; Measured: a 4-character source needs MaximumLength >= 5. On overflow this one writes
        ; nothing and leaves dst->Length as the caller had it -- unlike its sibling 018, which
        ; truncates and partially writes. Four functions in one family, two failure disciplines.
        ; Found by live substitution; see live-substitution/live_subst_ntconv2.c.
        lea       r11d, [rax + 1]                   ; the conversion plus its terminator
        cmp       r11d, r9d
        ja        overflow
        mov       word ptr [rcx], ax               ; dst->Length = n
        mov       r11, [rdx + 8]                   ; srcBuf
        mov       r10, [rcx + 8]                   ; dstBuf
        mov       r8d, eax                          ; n
        xor       r9d, r9d                          ; i
        lea       rcx, wia_upansimap                 ; table
a_loop:
        mov       edx, r8d
        sub       edx, r9d
        cmp       edx, 16
        jb        a_tail
        vmovdqu   ymm0, ymmword ptr [r11 + r9*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80]
        vptest    ymm1, ymm1
        jnz       a_sblock
        vpcmpgtw  ymm2, ymm0, ymmword ptr [C0060]
        vpcmpgtw  ymm3, ymm0, ymmword ptr [C007A]
        vpandn    ymm2, ymm3, ymm2
        vpand     ymm2, ymm2, ymmword ptr [C0020]
        vpsubw    ymm0, ymm0, ymm2
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
        mov       byte ptr [r10 + r8], 0            ; the terminator, at [Length]
        xor       eax, eax
        vzeroupper
        ret
overflow:
        mov       eax, 80000005h
        ret
alloc_unsupported:
        mov       eax, 0C000000Dh
        ret
wia_u2au ENDP
END
