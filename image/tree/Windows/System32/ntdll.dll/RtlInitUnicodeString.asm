; ntdll.dll!RtlInitUnicodeString  --  hand-written x86-64 reimplementation (1.76x vs shipped)
; source of truth: changes/094-rtlinitunicodestring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/094-rtlinitunicodestring/impl.asm
; void wia_rtlinitus(PUNICODE_STRING dest, PCWSTR src)   [Win64: rcx=dest, rdx=src]
;
; ntdll!RtlInitUnicodeString — sets dest->Buffer = src, dest->Length = wcslen(src)*2 (clamped
; so Length < MaximumLength <= 0xFFFE), dest->MaximumLength = Length + 2. src==NULL -> all
; zero. It is called constantly (every UNICODE_STRING setup) and ntdll's version makes a real
; `call` into its internal wcslen. We inline a page-safe AVX2 wcslen (change 001) so there is
; no call and the scan is 64 bytes/iteration, then fill the struct with the same 0xFFFE
; overflow clamp ntdll uses. UNICODE_STRING = {USHORT Length; USHORT MaximumLength; PWSTR
; Buffer@+8}. ISA: AVX2 + BMI1. Validated bit-exact vs live on Zen3.

.code
wia_rtlinitus PROC
        mov       r11, rcx                       ; r11 = dest (preserved across the scan)
        mov       qword ptr [r11 + 8], rdx       ; Buffer = src
        test      rdx, rdx
        jnz       have_src
        mov       dword ptr [r11], 0             ; src==NULL -> Length=MaximumLength=0
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
        ; rax = wchar count. bytes = 2*len, clamped so Length < MaximumLength <= 0xFFFE.
        add       rax, rax                       ; byte length
        mov       ecx, 0FFFCh
        cmp       rax, 0FFFEh
        cmovae    rax, rcx                       ; clamp Length to 0xFFFC
        mov       word ptr [r11], ax             ; Length
        add       ax, 2
        mov       word ptr [r11 + 2], ax         ; MaximumLength = Length + 2
        ret
wia_rtlinitus ENDP
END
