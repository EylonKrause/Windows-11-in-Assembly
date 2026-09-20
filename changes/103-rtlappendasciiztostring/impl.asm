; changes/103-rtlappendasciiztostring/impl.asm
; NTSTATUS wia_appendaz(PSTRING dest, PCSZ src)   [rcx=dest, rdx=src -> eax]
;
; ntdll!RtlAppendAsciizToString — appends the NUL-terminated ANSI `src` onto `dest` (a
; STRING / ANSI_STRING). Unlike the wide RtlAppendUnicodeToString it writes NO terminator:
;   src==NULL                                   -> STATUS_SUCCESS, no change
;   strlen(src) > 0xFFFE  OR  Length+strlen > MaximumLength -> STATUS_BUFFER_TOO_SMALL
;                                                  (0xC0000023), dest unchanged
;   else copy src after dest->Buffer[Length], Length += strlen.
; ntdll's version makes a real `call` into strlen and another into the copy; we inline the
; page-safe AVX2 strlen (change 032) + an SSE copy so short appends have no call overhead.
; String = {ushort Length; ushort MaximumLength; pchar Buffer@+8}. AVX2 + BMI1. Zen3.

.code
wia_appendaz PROC
        push      rbx
        push      rsi
        push      rdi
        test      rdx, rdx
        jz        ret_success                        ; src == NULL -> no-op
        mov       rbx, rcx                           ; dest
        mov       rsi, rdx                           ; src (kept for the copy)

        ; ---- inline AVX2 strlen(rsi) -> rax (bytes); clobbers rax,rcx,r8,r9,r10,ymm ----
        mov       rax, rsi
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vpcmpeqb  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb edx, ymm0
        shr       edx, cl
        test      edx, edx
        jnz       l_found_shift
        lea       r8, [r9 + 32]
        test      r8, 32
        jz        l_loop64
        vpcmpeqb  ymm0, ymm1, ymmword ptr [r8]
        vpmovmskb edx, ymm0
        test      edx, edx
        jnz       l_found_at_r8
        add       r8, 32
l_loop64:
        vmovdqa   ymm0, ymmword ptr [r8]
        vpminub   ymm2, ymm0, ymmword ptr [r8+32]
        vpcmpeqb  ymm3, ymm1, ymm2
        vpmovmskb edx, ymm3
        test      edx, edx
        jnz       l_found_in_block
        add       r8, 64
        jmp       l_loop64
l_found_in_block:
        vpcmpeqb  ymm3, ymm1, ymm0
        vpmovmskb ecx, ymm3
        test      ecx, ecx
        jnz       l_found_v0
        vpcmpeqb  ymm3, ymm1, ymmword ptr [r8+32]
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
        mov       rax, r10                           ; byte count
        jmp       have_len
l_found_shift:
        tzcnt     edx, edx
        mov       eax, edx

have_len:
        cmp       rax, 0FFFEh
        ja        ret_toosmall
        mov       r8d, eax                            ; addlen (bytes)
        movzx     edx, word ptr [rbx]                ; dest->Length
        movzx     r9d, word ptr [rbx + 2]            ; dest->MaximumLength
        lea       eax, [edx + r8d]                   ; newlen
        cmp       eax, r9d
        ja        ret_toosmall
        mov       word ptr [rbx], ax                 ; dest->Length = newlen
        mov       rdi, qword ptr [rbx + 8]
        add       rdi, rdx                           ; dest ptr = Buffer + old Length
        mov       ecx, r8d
c16:
        cmp       ecx, 16
        jb        ctail
        vmovdqu   xmm0, xmmword ptr [rsi]
        vmovdqu   xmmword ptr [rdi], xmm0
        add       rsi, 16
        add       rdi, 16
        sub       ecx, 16
        jmp       c16
ctail:                                                 ; 0..15 bytes, staged qword/dword/word/byte
        cmp       ecx, 8
        jb        ct4
        mov       rax, qword ptr [rsi]
        mov       qword ptr [rdi], rax
        add       rsi, 8
        add       rdi, 8
        sub       ecx, 8
ct4:
        cmp       ecx, 4
        jb        ct2
        mov       eax, dword ptr [rsi]
        mov       dword ptr [rdi], eax
        add       rsi, 4
        add       rdi, 4
        sub       ecx, 4
ct2:
        cmp       ecx, 2
        jb        ct1
        mov       ax, word ptr [rsi]
        mov       word ptr [rdi], ax
        add       rsi, 2
        add       rdi, 2
        sub       ecx, 2
ct1:
        test      ecx, ecx
        jz        cdone
        mov       al, byte ptr [rsi]
        mov       byte ptr [rdi], al
cdone:
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
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_appendaz ENDP
END
