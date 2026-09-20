; changes/096-rtlinitunicodestringex/impl.asm
; NTSTATUS wia_rtlinitusex(PUNICODE_STRING dest, PCWSTR src)   [Win64: rcx=dest, rdx=src]
;
; ntdll!RtlInitUnicodeStringEx — the validating variant of RtlInitUnicodeString (094): sets
; dest->Buffer = src; if wcslen(src) > 0x7FFE wchars it returns STATUS_NAME_TOO_LONG
; (0xC0000106) leaving Length=MaximumLength=0 (no clamp), else Length = wcslen*2,
; MaximumLength = Length + 2, and returns STATUS_SUCCESS. src==NULL -> zeroed, STATUS_SUCCESS.
; Same inline page-safe AVX2 wcslen (change 001) as 094 (no `call`). UNICODE_STRING =
; {Ushort Length; ushort MaximumLength; PWSTR Buffer@+8}. Isa: AVX2 + BMI1. Bit-exact vs live.

.code
wia_rtlinitusex PROC
        mov       r11, rcx                       ; r11 = dest (preserved across the scan)
        mov       dword ptr [r11], 0             ; Length=MaximumLength=0 (kept on NULL / too-long)
        mov       qword ptr [r11 + 8], rdx       ; Buffer = src
        test      rdx, rdx
        jnz       have_src
        xor       eax, eax                       ; src==NULL -> STATUS_SUCCESS
        ret
have_src:
        ; ---- inline AVX2 wcslen(rdx) -> rax (wchar count); clobbers rax,rcx,rdx,r8,r9,r10,ymm0-3 (not r11) ----
        mov       rax, rdx                       ; start (kept for length calc)
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rdx
        and       r9, -32
        mov       ecx, eax
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
        sub       r10, rax                       ; bytes to terminator
        mov       rax, r10
        shr       rax, 1                          ; wchar count
        vzeroupper
        jmp       have_len
l_found_shift:
        tzcnt     edx, edx
        mov       eax, edx
        shr       eax, 1
        vzeroupper
        ; fallthrough to have_len

have_len:
        ; rax = wchar count. > 0x7FFE -> STATUS_NAME_TOO_LONG (no clamp, leave L/M = 0).
        cmp       rax, 07FFEh
        ja        too_long
        add       rax, rax                       ; byte length
        mov       word ptr [r11], ax             ; Length
        add       ax, 2
        mov       word ptr [r11 + 2], ax         ; MaximumLength = Length + 2
        xor       eax, eax                       ; STATUS_SUCCESS
        ret
too_long:
        mov       eax, 0C0000106h                ; STATUS_NAME_TOO_LONG
        ret
wia_rtlinitusex ENDP
END
