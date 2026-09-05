; changes/098-rtlinitstringex/impl.asm
; NTSTATUS wia_rtlinitstrex(PSTRING dest, PCSZ src)   [Win64: rcx=dest, rdx=src]
;
; ntdll!RtlInitStringEx — the validating variant of RtlInitString (095): sets Buffer = src;
; if strlen(src) > 0xFFFE bytes returns STATUS_NAME_TOO_LONG (0xC0000106) leaving
; Length=MaximumLength=0 (no clamp), else Length = strlen, MaximumLength = Length + 1,
; STATUS_SUCCESS. src==NULL -> zeroed, STATUS_SUCCESS. Same inline page-safe AVX2 strlen
; (change 032, no `call`) as 095. STRING = {USHORT Length; USHORT MaximumLength; PCHAR
; Buffer@+8}. ISA: AVX2 + BMI1. Bit-exact vs live.

.code
wia_rtlinitstrex PROC
        mov       r11, rcx                       ; dest (preserved across the scan)
        mov       dword ptr [r11], 0             ; Length=MaximumLength=0 (kept on NULL / too-long)
        mov       qword ptr [r11 + 8], rdx       ; Buffer = src
        test      rdx, rdx
        jnz       have_src
        xor       eax, eax                       ; src==NULL -> STATUS_SUCCESS
        ret
have_src:
        ; ---- inline AVX2 strlen(rdx) -> rax (byte count); does not touch r11 ----
        mov       rax, rdx
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rdx
        and       r9, -32
        mov       ecx, eax
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
        mov       rax, r10                       ; byte length
        vzeroupper
        jmp       have_len
l_found_shift:
        tzcnt     edx, edx
        mov       eax, edx                       ; byte length
        vzeroupper
        ; fallthrough

have_len:
        ; rax = strlen. > 0xFFFE -> STATUS_NAME_TOO_LONG (no store, L/M stay 0).
        cmp       rax, 0FFFEh
        ja        too_long
        mov       word ptr [r11], ax             ; Length
        add       ax, 1
        mov       word ptr [r11 + 2], ax         ; MaximumLength = Length + 1
        xor       eax, eax                       ; STATUS_SUCCESS
        ret
too_long:
        mov       eax, 0C0000106h                ; STATUS_NAME_TOO_LONG
        ret
wia_rtlinitstrex ENDP
END
