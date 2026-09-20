; ntdll.dll!RtlUnicodeStringToAnsiString  --  hand-written x86-64 reimplementation (12.79x vs shipped)
; source of truth: changes/018-rtlunicodestringtoansistring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/018-rtlunicodestringtoansistring/impl.asm
; NTSTATUS wia_u2a(ANSI_STRING* dst, const UNICODE_STRING* src, BOOLEAN alloc)
;   [Win64: rcx, rdx, r8b -> eax]
;
; Reimplements the no-allocate path of ntdll!RtlUnicodeStringToAnsiString:
; narrows UTF-16 src to single-byte ANSI dst->Buffer (one byte per wchar) and sets
; dst->Length. All-ASCII 16-wchar blocks pack 16 -> 16 bytes (vpackuswb + vpermq);
; blocks with any wchar >= 0x80 use wia_ansimap[] (built from the OS codepage, so
; bit-exact). alloc=TRUE returns STATUS_INVALID_PARAMETER.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_ansimap:BYTE

.const
ALIGN 16
CFF80   DW      16 dup(0FF80h)

.code
wia_u2a PROC
        test      r8b, r8b
        jnz       alloc_unsupported
        movzx     eax, word ptr [rdx]              ; src Length (bytes)
        shr       eax, 1                            ; n = out bytes (wchar count)
        movzx     r9d, word ptr [rcx + 2]          ; dst MaximumLength
        ; The export nul-terminates, and it truncates rather than refusing.
        ;
        ; This used to be `cmp eax, r9d / ja overflow`, room for the conversion and nothing else --
        ; and the success path wrote no terminator. Both halves were wrong, and they were wrong
        ; together, which is why the change's own gate could not see either: it ran every case with
        ; MaximumLength fixed at 300 (so the size rule never bound) and compared only indices
        ; 0..Length-1 (so the terminator was never looked at).
        ;
        ; Measured over the whole MaximumLength range for a source of 4 and of 8 characters:
        ;     Max = 0            -> STATUS_BUFFER_OVERFLOW, nothing written, Length untouched
        ;     Max >= 1           -> n = min(srclen, Max - 1) characters are converted, a NUL is
        ;                           written at [n], Length = n, and the status is STATUS_SUCCESS
        ;                           when n == srclen and STATUS_BUFFER_OVERFLOW otherwise.
        ; So srclen 8 with Max 4 comes back as "ABC\0" with Length 3 and an overflow status, a
        ; Partial write on failure. Its three siblings do not do this: 020, 024, 019 and 025 refuse
        ; outright and leave the destination alone. Four functions in one family, two failure
        ; disciplines, measured rather than assumed.
        ;
        ; Found by live substitution; see live-substitution/live_subst_ntconv2.c.
        test      r9d, r9d
        jz        overflow                          ; no room even for the terminator
        dec       r9d                               ; avail = MaximumLength - 1
        xor       r10d, r10d                        ; prospective status = STATUS_SUCCESS
        cmp       eax, r9d
        jbe       a_fits
        mov       eax, r9d                          ; convert only what fits
        mov       r10d, 80000005h                   ; STATUS_BUFFER_OVERFLOW
a_fits:
        ; The status has to survive the conversion loop, which uses every volatile register. The
        ; caller's SHADOW SPACE is the natural place for it: this is a leaf, it allocates no frame,
        ; and [rsp+8] is the home slot the caller already reserved for rcx.
        mov       dword ptr [rsp + 8], r10d
        mov       word ptr [rcx], ax               ; dst->Length = n
        mov       r11, [rdx + 8]                   ; srcBuf
        mov       r10, [rcx + 8]                   ; dstBuf
        mov       r8d, eax                          ; n
        xor       r9d, r9d                          ; i
        lea       rcx, wia_ansimap                 ; table
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
        mov       byte ptr [r10 + r8], 0            ; the terminator, at [Length]
        mov       eax, dword ptr [rsp + 8]          ; SUCCESS, or OVERFLOW if we truncated
        vzeroupper
        ret
overflow:
        mov       eax, 80000005h                    ; MaximumLength == 0: nothing is written and
        ret                                         ; dst->Length is left as the caller had it
alloc_unsupported:
        mov       eax, 0C000000Dh
        ret
wia_u2a ENDP
END
