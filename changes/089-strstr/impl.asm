; changes/089-strstr/impl.asm
; char* wia_strstr(const char* haystack, const char* needle)   [Win64: rcx, rdx -> rax]
;
; First occurrence of the substring `needle` in `haystack`, else NULL. Empty needle
; -> haystack (C standard / ucrtbase).
;
; RESULT: PARKED. ucrtbase's strstr is a tuned SSE4.2 `pcmpistri` substring scan whose
; per-call cost is tiny; this AVX2 two-char-anchor streamer beats it only on large inputs
; (>=4 KB, up to 1.27x, ~28 GB/s vs ~22) and loses below ~2 KB, so a size class regresses.
; Kept as the strongest demonstration of the technique. See RESULTS.md.
;
; Method: two-char anchor (Muła), AVX2, ONE load per 32-byte block via a 1-block-lookahead
; pipeline. Broadcast needle[0]=first and needle[m-1]=last. A candidate start P needs
; H[P]==first and H[P+m-1]==last; the last byte can spill into the next block, so block K's
; candidates are processed once block K+1 is loaded, carrying its last-mask:
;   lmask64 = lmaskK | (lmask_{K+1} << 32);  cand = fmaskK & (uint32)(lmask64 >> (m-1))
; then needle[1..m-2] of each survivor is verified against loaded (mapped) bytes.
;
; Page-safe: 32-aligned base + prologue keep-mask; a block becomes the lookahead only after
; the prior block proved no terminator (string continues -> its aligned page is mapped). The
; terminator block is finished with a bounded scalar scan (also serves needles > 32 bytes).
; Frame: rbx/rsi/rdi/r12/r13/r14/r15. ISA: AVX2 + BMI1. Zen3.

.code
wia_strstr PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        cmp       byte ptr [rdx], 0
        jne       have_needle
        mov       rax, rcx
        jmp       epi
have_needle:
        mov       rdi, rdx
        xor       rsi, rsi
nlen:
        cmp       byte ptr [rdi + rsi], 0
        je        nlen_done
        inc       rsi
        jmp       nlen
nlen_done:
        mov       r13, rsi
        dec       r13
        cmp       rsi, 32
        ja        fb_scalar

        vpbroadcastb ymm2, byte ptr [rdi]
        lea       rax, [rdi + r13]
        vpbroadcastb ymm3, byte ptr [rax]
        vpxor     ymm1, ymm1, ymm1

        mov       rbx, rcx
        and       rbx, -32
        and       ecx, 31
        mov       r9d, -1
        shl       r9d, cl
        vmovdqu   ymm4, ymmword ptr [rbx]
        vpcmpeqb  ymm5, ymm4, ymm1
        vpmovmskb r11d, ymm5
        and       r11d, r9d
        vpcmpeqb  ymm0, ymm4, ymm2
        vpmovmskb r10d, ymm0
        and       r10d, r9d
        test      r11d, r11d
        jnz       prol_term
        vpcmpeqb  ymm0, ymm4, ymm3
        vpmovmskb eax, ymm0
        mov       r15d, r10d
        mov       r8d, eax
        shl       r8, 32
        or        r15, r8
        mov       r14, rbx
        add       rbx, 32

main:
        vmovdqu   ymm4, ymmword ptr [rbx]
        vpcmpeqb  ymm5, ymm4, ymm1
        vpmovmskb r11d, ymm5
        vpcmpeqb  ymm0, ymm4, ymm2
        vpmovmskb r10d, ymm0
        vpcmpeqb  ymm0, ymm4, ymm3
        vpmovmskb eax, ymm0

        mov       rdx, r15
        shr       rdx, 32
        mov       r8d, eax
        shl       r8, 32
        or        rdx, r8
        mov       ecx, r13d
        shr       rdx, cl
        mov       r9d, r15d
        and       r9d, edx
pcand:
        test      r9d, r9d
        jz        pend_done
        tzcnt     ecx, r9d
        lea       r12, [r14 + rcx]
        cmp       rsi, 2
        jbe       phit
        mov       r8, 1
pmid:
        movzx     edx, byte ptr [rdi + r8]
        cmp       dl, byte ptr [r12 + r8]
        jne       pmfail
        inc       r8
        cmp       r8, r13
        jb        pmid
phit:
        mov       rax, r12
        vzeroupper
        jmp       epi
pmfail:
        blsr      r9d, r9d
        jmp       pcand
pend_done:
        test      r11d, r11d
        jnz       main_term
        mov       r15d, r10d
        mov       r8d, eax
        shl       r8, 32
        or        r15, r8
        mov       r14, rbx
        add       rbx, 32
        jmp       main
main_term:
        mov       r8, rbx
        jmp       scal

prol_term:
        tzcnt     ecx, r9d
        lea       r8, [rbx + rcx]
        jmp       scal
fb_scalar:
        mov       r8, rcx
scal:
        cmp       byte ptr [r8], 0
        je        ret_null
        mov       r9, rdi
        mov       r10, r8
sc_cmp:
        movzx     eax, byte ptr [r9]
        test      al, al
        jz        sc_hit
        cmp       al, byte ptr [r10]
        jne       sc_adv
        inc       r9
        inc       r10
        jmp       sc_cmp
sc_adv:
        inc       r8
        jmp       scal
sc_hit:
        mov       rax, r8
        vzeroupper
        jmp       epi

ret_null:
        xor       eax, eax
        vzeroupper
epi:
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_strstr ENDP
END
