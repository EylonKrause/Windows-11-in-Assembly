; ntdll.dll!RtlHashUnicodeString  --  hand-written x86-64 reimplementation (4.50x vs shipped)
; source of truth: changes/009-rtlhashunicodestring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
;  changes/009-rtlhashunicodestring/impl.asm
;  NTSTATUS wia_rtlhash(const UNICODE_STRING* s, BOOLEAN ci, ULONG algo, PULONG out)
;    [Win64: rcx, dl, r8d, r9 -> eax=STATUS, *out = hash]
;
;  Reimplements ntdll!RtlHashUnicodeString (default X65599). The sequential
;  h = h*65599 + c chain is broken by processing 8 chars/block:
;    h = h*P^8 + (c0*P^7 + ... + c7*P^0)  via one vpmulld + a horizontal add.
;  Case-insensitive upcases each block in-register for the all-ASCII common case
;  (a-z range subtract, no store/reload) and falls back to a wia_upcase[] lookup
;  for blocks containing any wchar >= 0x80. Validated in C vs live ntdll.
;
;  ISA: AVX2. Validated on Zen3.

EXTERN wia_upcase:WORD
P8 EQU 0D319BE01h

.const
ALIGN 16
Wvec    DD      0A311B1BFh, 0D62AEE81h, 0162C613Fh, 043EC5F01h, 02E86D0BFh, 0007E0F81h, 00001003Fh, 000000001h
C0060   DW      8 dup(0060h)
C007A   DW      8 dup(007Ah)
C0020   DW      8 dup(0020h)
CFF80   DW      8 dup(0FF80h)

.code
wia_rtlhash PROC
        movzx     r10d, word ptr [rcx]             ; Length (bytes)
        mov       r11, [rcx + 8]                   ; Buffer
        shr       r10d, 1                          ; n = wchar count
        xor       eax, eax                         ; h
        vmovdqu   ymm5, ymmword ptr [Wvec]
        mov       ecx, P8
        test      dl, dl
        jnz       ci_path

; ---- case-sensitive ----
        xor       edx, edx
cs_loop:
        lea       r8d, [rdx + 8]
        cmp       r8d, r10d
        jg        cs_tail
        vpmovzxwd ymm0, xmmword ptr [r11 + rdx*2]
        vpmulld   ymm0, ymm0, ymm5
        vextracti128 xmm1, ymm0, 1
        vpaddd    xmm0, xmm0, xmm1
        vpshufd   xmm1, xmm0, 04Eh
        vpaddd    xmm0, xmm0, xmm1
        vpshufd   xmm1, xmm0, 0B1h
        vpaddd    xmm0, xmm0, xmm1
        vmovd     r8d, xmm0
        imul      eax, ecx
        add       eax, r8d
        add       edx, 8
        jmp       cs_loop
cs_tail:
        cmp       edx, r10d
        jae       done
        movzx     r8d, word ptr [r11 + rdx*2]
        imul      eax, eax, 65599
        add       eax, r8d
        inc       edx
        jmp       cs_tail
done:
        mov       dword ptr [r9], eax
        xor       eax, eax
        vzeroupper
        ret

; ---- case-insensitive ----
ci_path:
        push      r13
        sub       rsp, 48
        lea       r13, wia_upcase
        xor       edx, edx
ci_loop:
        lea       r8d, [rdx + 8]
        cmp       r8d, r10d
        jg        ci_tail
        vmovdqu   xmm0, xmmword ptr [r11 + rdx*2]   ; 8 wchars
        vpand     xmm1, xmm0, xmmword ptr [CFF80]
        vptest    xmm1, xmm1
        jnz       ci_scalar_block                   ; any wchar >= 0x80
        ; in-register ASCII upcase: w -= 0x20 where 0x60 < w <= 0x7A
        vpcmpgtw  xmm1, xmm0, xmmword ptr [C0060]   ; w > 0x60
        vpcmpgtw  xmm2, xmm0, xmmword ptr [C007A]   ; w > 0x7A
        vpandn    xmm1, xmm2, xmm1                  ; (w>0x60) & !(w>0x7A)
        vpand     xmm1, xmm1, xmmword ptr [C0020]
        vpsubw    xmm0, xmm0, xmm1
ci_have_block:
        vpmovzxwd ymm0, xmm0
        vpmulld   ymm0, ymm0, ymm5
        vextracti128 xmm1, ymm0, 1
        vpaddd    xmm0, xmm0, xmm1
        vpshufd   xmm1, xmm0, 04Eh
        vpaddd    xmm0, xmm0, xmm1
        vpshufd   xmm1, xmm0, 0B1h
        vpaddd    xmm0, xmm0, xmm1
        vmovd     r8d, xmm0
        imul      eax, ecx
        add       eax, r8d
        add       edx, 8
        jmp       ci_loop
ci_scalar_block:
        movzx     r8d, word ptr [r11 + rdx*2 + 0]
        movzx     r8d, word ptr [r13 + r8*2]
        mov       word ptr [rsp + 0], r8w
        movzx     r8d, word ptr [r11 + rdx*2 + 2]
        movzx     r8d, word ptr [r13 + r8*2]
        mov       word ptr [rsp + 2], r8w
        movzx     r8d, word ptr [r11 + rdx*2 + 4]
        movzx     r8d, word ptr [r13 + r8*2]
        mov       word ptr [rsp + 4], r8w
        movzx     r8d, word ptr [r11 + rdx*2 + 6]
        movzx     r8d, word ptr [r13 + r8*2]
        mov       word ptr [rsp + 6], r8w
        movzx     r8d, word ptr [r11 + rdx*2 + 8]
        movzx     r8d, word ptr [r13 + r8*2]
        mov       word ptr [rsp + 8], r8w
        movzx     r8d, word ptr [r11 + rdx*2 + 10]
        movzx     r8d, word ptr [r13 + r8*2]
        mov       word ptr [rsp + 10], r8w
        movzx     r8d, word ptr [r11 + rdx*2 + 12]
        movzx     r8d, word ptr [r13 + r8*2]
        mov       word ptr [rsp + 12], r8w
        movzx     r8d, word ptr [r11 + rdx*2 + 14]
        movzx     r8d, word ptr [r13 + r8*2]
        mov       word ptr [rsp + 14], r8w
        vmovdqu   xmm0, xmmword ptr [rsp]
        jmp       ci_have_block
ci_tail:
        cmp       edx, r10d
        jae       ci_done
        movzx     r8d, word ptr [r11 + rdx*2]
        movzx     r8d, word ptr [r13 + r8*2]
        imul      eax, eax, 65599
        add       eax, r8d
        inc       edx
        jmp       ci_tail
ci_done:
        mov       dword ptr [r9], eax
        xor       eax, eax
        add       rsp, 48
        pop       r13
        vzeroupper
        ret
wia_rtlhash ENDP
END
