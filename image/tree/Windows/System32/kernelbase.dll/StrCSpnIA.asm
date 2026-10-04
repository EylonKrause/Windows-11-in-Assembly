; kernelbase.dll!StrCSpnIA  --  hand-written x86-64 reimplementation (600x vs shipped)
; source of truth: changes/315-strcspnia/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/315-strcspnia/impl.asm
;   int wia_strcspnia(const char* s, const char* set)        [Win64: rcx, rdx -> eax]
;
; shlwapi!StrCSpnIA (body in kernelbase, RVA 0x12CD00): the number of leading characters of s that match
; no character of set, case-insensitively. The export walks s with CharNextA and calls StrChrIA(set, c)
; for every character -- which compares c with each character of the set through CompareStringA -- so
; it costs ~65 ns per (character x set character). On a single-byte code page the relation is fixed and
; symmetric (tables.c), so the set becomes a 256-bit bitmap and s is tested 32 bytes at a time:
;   row = pshufb(T0, x) | pshufb(T1, x ^ 0x80)     the bitmap row for x's low nibble, by x's top bit
;   bit = pshufb(BITS, (x >> 4) & 15)              1 << (x's high nibble & 7)
;   member = (row & bit) == bit
;
; What the export makes observable, and is kept:
;   s or set NULL, or s empty: 0, and the set is not read.
;   The first character is searched for in the set the way StrChrIA does it -- stopping at a match --
;   so a set that is unterminated beyond a byte matching s[0] is read only that far (plus one byte: the
;   WORD read at the match). Only when s[0] matches nothing has the whole set been read, and only then
;   is the bitmap built from it.
;   The WORD at every position of s is read: at a match, the byte after it is touched too.
; Loads are aligned 32-byte blocks, which never touch a page the export does not.
;
; Hand-off: wia_scs_fb (DBCS code page, an unsuitable relation, no AVX2/BMI1/BMI2), registers untouched.
; Registers: rax, r8, r9, r10, ymm0-ymm5. Leaf: no stack, no unwind data needed.

OPTION PROC:PRIVATE
PUBLIC wia_strcspnia
EXTERN wia_scs_memb:BYTE
EXTERN wia_scs_bits:BYTE
EXTERN wia_scs_fb:DWORD
EXTERN wia_scs_fb_cspn:QWORD

.const
ALIGN 16
scs_c80   DB 32 DUP (80h)
scs_c0f   DB 32 DUP (0Fh)
scs_bitv  DB 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128
          DB 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128

.code

; ymm5 = 0xFF where the byte at addr is in the set or is the NUL. ymm1 = T0, ymm2 = T1, ymm3 = BITS.
MEMB MACRO addr
        vmovdqa   ymm4, ymmword ptr addr
        vpshufb   ymm5, ymm1, ymm4              ; T0 row, zero where x >= 0x80
        vpxor     ymm0, ymm4, ymmword ptr [scs_c80]
        vpshufb   ymm0, ymm2, ymm0              ; T1 row, zero where x < 0x80
        vpor      ymm5, ymm5, ymm0
        vpsrlw    ymm0, ymm4, 4
        vpand     ymm0, ymm0, ymmword ptr [scs_c0f]
        vpshufb   ymm0, ymm3, ymm0              ; 1 << (high nibble & 7)
        vpand     ymm5, ymm5, ymm0
        vpcmpeqb  ymm5, ymm5, ymm0              ; member
        vpxor     xmm0, xmm0, xmm0
        vpcmpeqb  ymm4, ymm4, ymm0              ; the NUL
        vpor      ymm5, ymm5, ymm4
ENDM

ALIGN 16
wia_strcspnia PROC
        cmp       dword ptr [wia_scs_fb], 0
        jne       l_fb
        xor       eax, eax
        test      rcx, rcx
        jz        l_ret
        test      rdx, rdx
        jz        l_ret
        movzx     r8d, byte ptr [rcx]
        test      r8d, r8d
        jz        l_ret
        movzx     eax, byte ptr [rcx + 1]       ; the export reads the WORD at s

        ; 1. s[0] against the set, as StrChrIA(set, s[0]) does it
        lea       r9, wia_scs_memb
        vpbroadcastb ymm1, byte ptr [r9 + r8*2]
        vpbroadcastb ymm2, byte ptr [r9 + r8*2 + 1]
        vpxor     xmm0, xmm0, xmm0
        mov       rax, rdx
        and       rax, -32
        vmovdqa   ymm3, ymmword ptr [rax]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpor      ymm4, ymm4, ymm3
        vpmovmskb r9d, ymm4
        mov       r10d, edx
        and       r10d, 31
        shrx      r9d, r9d, r10d
        test      r9d, r9d
        jz        f_loop
        tzcnt     r9d, r9d
        add       r9, rdx
        jmp       f_found
f_loop:
        add       rax, 32
        vmovdqa   ymm3, ymmword ptr [rax]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpor      ymm4, ymm4, ymm3
        vptest    ymm4, ymm4
        jz        f_loop
        vpmovmskb r9d, ymm4
        tzcnt     r9d, r9d
        add       r9, rax
f_found:
        cmp       byte ptr [r9], 0
        je        l_build                       ; no match: the whole set has been read, r9 = its NUL
        movzx     eax, byte ptr [r9 + 1]        ; StrChrIA read the WORD at the match
        vzeroupper
        xor       eax, eax
        ret

        ; 2. the set's bitmap: the OR of its bytes' rows
l_build:
        vpxor     xmm1, xmm1, xmm1
        lea       r10, wia_scs_bits
        mov       rax, rdx
        cmp       rax, r9
        jae       l_built
l_bloop:
        movzx     r8d, byte ptr [rax]
        shl       r8d, 5
        vpor      ymm1, ymm1, ymmword ptr [r10 + r8]
        inc       rax
        cmp       rax, r9
        jb        l_bloop
l_built:
        vpermq    ymm2, ymm1, 0EEh              ; T1 = the rows for bytes 0x80..0xFF, both lanes
        vpermq    ymm1, ymm1, 044h              ; T0 = the rows for bytes 0x00..0x7F, both lanes
        vmovdqu   ymm3, ymmword ptr [scs_bitv]

        ; 3. s from its second character
        lea       r9, [rcx + 1]
        mov       rax, r9
        and       rax, -32
        MEMB      [rax]
        vpmovmskb r10d, ymm5
        mov       r8d, r9d
        and       r8d, 31
        shrx      r10d, r10d, r8d
        test      r10d, r10d
        jz        s_loop
        tzcnt     r10d, r10d
        add       r9, r10
        jmp       s_found
s_loop:
        add       rax, 32
        MEMB      [rax]
        vptest    ymm5, ymm5
        jz        s_loop
        vpmovmskb r9d, ymm5
        tzcnt     r9d, r9d
        add       r9, rax
s_found:
        vzeroupper
        cmp       byte ptr [r9], 0
        je        s_ret
        movzx     eax, byte ptr [r9 + 1]        ; the export read the WORD at the stopping character
s_ret:
        mov       rax, r9
        sub       rax, rcx                      ; (int) of the distance, as the export's sub ebx, edi
l_ret:
        ret
l_fb:
        jmp       qword ptr [wia_scs_fb_cspn]
wia_strcspnia ENDP

END
