; ucrtbase.dll!strpbrk  --  hand-written x86-64 reimplementation (3.53x vs shipped)
; source of truth: changes/038-strpbrk/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/038-strpbrk/impl.asm
; char* wia_strpbrk(const char* str, const char* set)   [Win64: rcx, rdx -> rax]
;
; Pointer to the first byte of str that is a member of set, else NULL. The byte
; (narrow) counterpart of 035-wcspbrk; used pervasively in ASCII / UTF-8 parsing.
; ucrtbase does the naive O(n*m) scan (~2.2 GB/s).
;
; Scan str 32 bytes at a time; per block OR vpcmpeqb against every set byte (broadcast
; straight from memory -> no per-call setup, no stack frame) and the ==0 terminator mask,
; then take the first "stop" position: a set match -> return its pointer; the terminator
; -> NULL. Page-safe: 32-aligned base + prologue mask-shift, then a 32-aligned loop, so no
; load crosses into a page str does not already occupy. Sets >= 32 bytes take a correct
; scalar fallback; empty set -> NULL. No non-volatile regs, no stack.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strpbrk PROC
        mov       r8, rcx                          ; str
        mov       r9, rdx                          ; set
        xor       r10, r10                          ; slen
sl_lp:
        cmp       byte ptr [r9 + r10], 0
        je        sl_done
        inc       r10
        cmp       r10, 32
        jae       scalar_setup
        jmp       sl_lp
sl_done:
        test      r10, r10
        jz        ret_null                          ; empty set -> NULL

        vpxor     ymm1, ymm1, ymm1                  ; ymm1 = 0
        mov       r11, r8
        and       r11, -32                          ; aligned-down block base
        mov       ecx, r8d
        and       ecx, 31                           ; cl = start byte offset within block

        ; ---- prologue block ----
        vmovdqu   ymm6, ymmword ptr [r11]
        vpcmpeqb  ymm3, ymm6, ymm1                  ; == 0 (terminator)
        vpxor     ymm4, ymm4, ymm4                  ; in-set accumulator
        xor       rax, rax
in_lp0:
        vpbroadcastb ymm0, byte ptr [r9 + rax]
        vpcmpeqb  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp0
        vpor      ymm5, ymm4, ymm3                  ; stop = in-set | terminator
        vpmovmskb edx, ymm5
        vpmovmskb eax, ymm4                         ; in-set mask
        shr       edx, cl                           ; relative to str
        shr       eax, cl
        test      edx, edx
        jz        nextblk
        tzcnt     edx, edx                          ; byte offset from str
        bt        eax, edx
        jnc       ret_null                          ; stop was terminator -> NULL
        lea       rax, [r8 + rdx]
        vzeroupper
        ret

nextblk:
        add       r11, 32
        vmovdqu   ymm6, ymmword ptr [r11]
        vpcmpeqb  ymm3, ymm6, ymm1
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
in_lp1:
        vpbroadcastb ymm0, byte ptr [r9 + rax]
        vpcmpeqb  ymm0, ymm6, ymm0
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp1
        vpor      ymm5, ymm4, ymm3
        vpmovmskb edx, ymm5
        test      edx, edx
        jz        nextblk
        vpmovmskb eax, ymm4
        tzcnt     edx, edx
        bt        eax, edx
        jnc       ret_null
        lea       rax, [r11 + rdx]
        vzeroupper
        ret

        ; ---- scalar fallback (set >= 32 bytes) ----
scalar_setup:
        xor       rax, rax                           ; index
sc_next:
        movzx     r10d, byte ptr [r8 + rax]
        test      r10b, r10b
        jz        ret_null                           ; terminator -> NULL
        xor       rcx, rcx
sc_in:
        movzx     edx, byte ptr [r9 + rcx]
        test      dl, dl
        jz        sc_adv                             ; end of set -> not a member
        cmp       dl, r10b
        je        sc_hit
        inc       rcx
        jmp       sc_in
sc_hit:
        lea       rax, [r8 + rax]
        vzeroupper
        ret
sc_adv:
        inc       rax
        jmp       sc_next

ret_null:
        xor       eax, eax
        vzeroupper
        ret
wia_strpbrk ENDP
END
