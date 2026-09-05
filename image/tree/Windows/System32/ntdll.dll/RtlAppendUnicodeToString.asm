; ntdll.dll!RtlAppendUnicodeToString  --  hand-written x86-64 reimplementation (1.42x vs shipped)
; source of truth: changes/101-rtlappendunicodetostring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/101-rtlappendunicodetostring/impl.asm
; NTSTATUS wia_appendus(PUNICODE_STRING dest, PCWSTR src)   [rcx=dest, rdx=src -> eax]
;
; ntdll!RtlAppendUnicodeToString — appends the NUL-terminated wide `src` onto `dest`:
;   src==NULL                                 -> STATUS_SUCCESS, no change
;   wcslen(src) > 0x7FFE  OR  Length+2*len > MaximumLength -> STATUS_BUFFER_TOO_SMALL
;                                                (0xC0000023), dest unchanged
;   else copy src after dest->Buffer[Length], Length += 2*len, and (if MaximumLength-Length
;        >= 2) write a wide NUL.
; ntdll's version makes a real `call` into wcslen and another into the memcpy; we inline the
; page-safe AVX2 wcslen (change 001) and a SIMD copy, so short appends have no call overhead.
; UNICODE_STRING = {USHORT Length; USHORT MaximumLength; PWSTR Buffer@+8}. AVX2 + BMI1. Zen3.

.code
wia_appendus PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        test      rdx, rdx
        jz        ret_success                        ; src == NULL -> success, no-op
        mov       rbx, rcx                           ; dest
        mov       rsi, rdx                           ; src (kept for the copy)

        ; ---- inline AVX2 wcslen(rsi) -> rax (wchars); clobbers rax,rcx,r8,r9,r10,ymm ----
        mov       rax, rsi
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vpcmpeqw  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb edx, ymm0
        shr       edx, cl
        test      edx, edx
        jnz       l_found_shift
        lea       r8, [r9 + 32]
        test      r8, 32
        jz        l_loop64
        vpcmpeqw  ymm0, ymm1, ymmword ptr [r8]
        vpmovmskb edx, ymm0
        test      edx, edx
        jnz       l_found_at_r8
        add       r8, 32
l_loop64:
        vmovdqa   ymm0, ymmword ptr [r8]
        vpminuw   ymm2, ymm0, ymmword ptr [r8+32]
        vpcmpeqw  ymm3, ymm1, ymm2
        vpmovmskb edx, ymm3
        test      edx, edx
        jnz       l_found_in_block
        add       r8, 64
        jmp       l_loop64
l_found_in_block:
        vpcmpeqw  ymm3, ymm1, ymm0
        vpmovmskb ecx, ymm3
        test      ecx, ecx
        jnz       l_found_v0
        vpcmpeqw  ymm3, ymm1, ymmword ptr [r8+32]
        vpmovmskb edx, ymm3
        tzcnt     edx, edx
        lea       r10, [r8 + 32]
        add       r10, rdx
        jmp       l_finish
l_found_v0:
        tzcnt     ecx, ecx
        lea       r10, [r8 + rcx]
        jmp       l_finish
l_found_at_r8:
        tzcnt     edx, edx
        lea       r10, [r8 + rdx]
l_finish:
        sub       r10, rax
        mov       rax, r10
        shr       rax, 1                             ; wchar count
        jmp       have_len
l_found_shift:
        tzcnt     edx, edx
        mov       eax, edx
        shr       eax, 1

have_len:
        ; rax = wchar count of src
        cmp       rax, 07FFEh
        ja        ret_toosmall
        lea       r8d, [eax + eax]                   ; addlen (bytes)
        movzx     edx, word ptr [rbx]                ; dest->Length
        movzx     r12d, word ptr [rbx + 2]           ; dest->MaximumLength
        lea       eax, [edx + r8d]                   ; newlen
        cmp       eax, r12d
        ja        ret_toosmall
        mov       r9d, r12d
        sub       r9d, eax                           ; room = MaximumLength - newlen
        mov       word ptr [rbx], ax                 ; dest->Length = newlen
        mov       rdi, qword ptr [rbx + 8]
        add       rdi, rdx                           ; dest ptr = Buffer + old Length
        mov       ecx, r8d                            ; bytes to copy
copy16:
        cmp       ecx, 16
        jb        copytail
        vmovdqu   xmm0, xmmword ptr [rsi]
        vmovdqu   xmmword ptr [rdi], xmm0
        add       rsi, 16
        add       rdi, 16
        sub       ecx, 16
        jmp       copy16
copytail:
        test      ecx, ecx
        jz        copy_done
        mov       ax, word ptr [rsi]
        mov       word ptr [rdi], ax
        add       rsi, 2
        add       rdi, 2
        sub       ecx, 2
        jmp       copytail
copy_done:
        cmp       r9d, 2
        jb        ret_success_v
        mov       word ptr [rdi], 0                  ; wide NUL at Buffer + newlen
ret_success_v:
        xor       eax, eax
        vzeroupper
        jmp       epi
ret_toosmall:
        mov       eax, 0C0000023h                    ; STATUS_BUFFER_TOO_SMALL
        vzeroupper
        jmp       epi
ret_success:
        xor       eax, eax
epi:
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_appendus ENDP
END
