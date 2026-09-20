; ntdll.dll!RtlMultiByteToUnicodeN  --  hand-written x86-64 reimplementation (4.05x vs shipped)
; source of truth: changes/022-rtlmultibytetounicoden/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/022-rtlmultibytetounicoden/impl.asm
; NTSTATUS wia_mb2u(wchar_t* dst, ulong maxBytes, pulong outLen, const char* src, ulong srcBytes)
;   [Win64: rcx, edx, r8, r9, [rsp+0x28] -> eax; *outLen = bytes written]
;
; Reimplements ntdll!RtlMultiByteToUnicodeN (single-byte ANSI -> UTF-16, one wchar
; per byte). Writes up to maxBytes; *outLen = bytes written; STATUS_SUCCESS.
; All-ASCII 16/8-byte blocks widen with vpmovzxbw; other bytes via the 256-entry
; wia_a2umap[] byte->wchar table from the OS codepage.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_a2umap:WORD

.code
wia_mb2u PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        mov       rbx, rcx                          ; dst (wchar)
        mov       edx, edx                          ; maxBytes (in edx already)
        shr       edx, 1                            ; maxWchars
        mov       edi, edx                          ; maxWchars
        mov       r12, r8                           ; outLen ptr
        mov       rsi, r9                           ; src (bytes)
        mov       r13d, dword ptr [rsp + 50h]       ; srcBytes = n (byte count)
        xor       r9d, r9d                          ; i (wchar/byte index)
        lea       rcx, wia_a2umap
mu_loop:
        cmp       r9d, r13d
        jae       mu_done
        cmp       r9d, edi
        jae       mu_done                           ; output full (wchars)
        mov       r8d, r13d
        sub       r8d, r9d
        cmp       r8d, 16
        jb        mu_a8
        mov       r8d, edi
        sub       r8d, r9d
        cmp       r8d, 16
        jb        mu_a8
        vmovdqu   xmm0, xmmword ptr [rsi + r9]
        vpmovmskb eax, xmm0
        test      eax, eax
        jnz       mu_a8
        vpmovzxbw ymm0, xmm0
        vmovdqu   ymmword ptr [rbx + r9*2], ymm0
        add       r9, 16
        jmp       mu_loop
mu_a8:
        mov       r8d, r13d
        sub       r8d, r9d
        cmp       r8d, 8
        jb        mu_scalar
        mov       r8d, edi
        sub       r8d, r9d
        cmp       r8d, 8
        jb        mu_scalar
        vmovq     xmm0, qword ptr [rsi + r9]
        vpmovmskb eax, xmm0
        and       eax, 0FFh
        jnz       mu_scalar
        vpmovzxbw xmm0, xmm0
        vmovdqu   xmmword ptr [rbx + r9*2], xmm0
        add       r9, 8
        jmp       mu_loop
mu_scalar:
        movzx     eax, byte ptr [rsi + r9]
        movzx     eax, word ptr [rcx + rax*2]
        mov       word ptr [rbx + r9*2], ax
        add       r9, 1
        jmp       mu_loop
mu_done:
        lea       eax, [r9*2]
        mov       dword ptr [r12], eax              ; *outLen = bytes written
        xor       eax, eax
        vzeroupper
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_mb2u ENDP
END
