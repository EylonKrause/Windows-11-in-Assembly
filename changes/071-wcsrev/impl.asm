; changes/071-wcsrev/impl.asm
; wchar_t* wia_wcsrev(wchar_t* s)   [Win64: rcx -> rax (returns s)]
;
; Wide (UTF-16) sibling of 070 _strrev. ucrtbase!_wcsrev is the exact wchar analog of
; _strrev: a scalar 2-byte-at-a-time wcslen (serial pointer chain) then a scalar two-pointer
; inward WORD swap. We beat it at every size the same way, at wchar granularity:
;   * Length: an unrolled scalar probe over the first 16 wchars (independent lea+cmp+je per
;     position, no serial chain, one branch each -> word loads pipeline ~2/cycle). Each cmp
;     runs only if the prior wchar was non-zero, so it never retires a read past the NUL --
;     as page-safe as ucrtbase's scalar wcslen. Only past 16 wchars do we switch to a page-
;     safe 16-byte vpcmpeqw length scan.
;   * Reverse: byte-reverse whole registers instead of scalar swaps, writing only wchars
;     inside [lo,hi): >= 32 B swaps 16-byte (8-wchar) vpshufb blocks with a WORD-reverse mask
;     from both ends; 16..31 B swaps 8-byte (4-wchar) vpshuflw blocks; an exact 8-byte
;     remainder is one vpshuflw; the rest is a scalar word swap.
; Only volatile registers -> no push/pop prologue. All-VEX-128 (no AVX/SSE transition).
; ISA: AVX + SSE2 (vpshuflw). Validated bit-exact vs ucrtbase on Zen3.

.const
ALIGN 16
revmw db 14,15,12,13,10,11,8,9,6,7,4,5,2,3,0,1     ; reverse 8 words within a 16-byte lane

.code
wia_wcsrev PROC
        mov       r8, rcx                            ; original s (return value)

        ; ---- unrolled wcslen probe: first 16 wchars (stops at NUL, never reads past it) ----
        lea       rax, [rcx]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 2]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 4]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 6]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 8]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 10]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 12]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 14]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 16]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 18]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 20]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 22]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 24]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 26]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 28]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 30]
        cmp       word ptr [rax], 0
        je        sl_done

        ; ---- not terminated in 16 wchars: long string, page-safe vector scan from here ----
        lea       rax, [rcx + 32]
        vpxor     xmm1, xmm1, xmm1
slv:
        mov       r9, rax
        and       r9, 4095
        cmp       r9, 4080                           ; within 16 of a page end?
        ja        slv_scalar
        vmovdqu   xmm0, xmmword ptr [rax]
        vpcmpeqw  xmm0, xmm0, xmm1                   ; per-WORD zero test
        vpmovmskb r9d, xmm0
        test      r9d, r9d
        jnz       slv_found
        add       rax, 16
        jmp       slv
slv_scalar:
        cmp       word ptr [rax], 0
        je        sl_done
        add       rax, 2
        jmp       slv
slv_found:
        bsf       r9d, r9d                           ; low bit of first zero word = its byte offset
        add       rax, r9
sl_done:
        ; rax -> NUL wchar. lo = rcx (= s, untouched), hi = rax (one past last wchar).
        mov       rdx, rax                           ; hi

        ; ---- tier 1: 16-byte (8-wchar) vpshufb block swaps (remaining >= 32) ----
        mov       rax, rdx
        sub       rax, rcx                           ; remaining bytes
        cmp       rax, 32
        jb        blk8
        vmovdqa   xmm2, xmmword ptr [revmw]
rloop:
        vmovdqu   xmm0, xmmword ptr [rcx]            ; front
        vmovdqu   xmm3, xmmword ptr [rdx - 16]       ; back
        vpshufb   xmm0, xmm0, xmm2
        vpshufb   xmm3, xmm3, xmm2
        vmovdqu   xmmword ptr [rcx], xmm3            ; front <- reverse(back)
        vmovdqu   xmmword ptr [rdx - 16], xmm0       ; back  <- reverse(front)
        add       rcx, 16
        sub       rdx, 16
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 32
        jae       rloop

        ; ---- tier 2: 8-byte (4-wchar) vpshuflw block swaps (remaining in [16,31]) ----
blk8:
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 16
        jb        rem8
        vmovq     xmm0, qword ptr [rcx]              ; front 4 wchars
        vmovq     xmm3, qword ptr [rdx - 8]          ; back 4 wchars
        vpshuflw  xmm0, xmm0, 1Bh                    ; reverse the 4 words
        vpshuflw  xmm3, xmm3, 1Bh
        vmovq     qword ptr [rcx], xmm3              ; front <- reverse(back)
        vmovq     qword ptr [rdx - 8], xmm0          ; back  <- reverse(front)
        add       rcx, 8
        sub       rdx, 8
        jmp       blk8

        ; ---- tier 3: exact 8-byte (4-wchar) remainder -> single vpshuflw (common short case) ----
rem8:
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 8
        jne       rmid
        vmovq     xmm0, qword ptr [rcx]
        vpshuflw  xmm0, xmm0, 1Bh
        vmovq     qword ptr [rcx], xmm0
        jmp       rdone

        ; ---- tier 4: scalar two-pointer word swap (remaining 2/4/6 or 10/12/14) ----
rmid:
        lea       rax, [rdx - 2]
        cmp       rcx, rax
        jae       rdone
        movzx     r9d, word ptr [rcx]
        movzx     r10d, word ptr [rdx - 2]
        mov       word ptr [rcx], r10w
        mov       word ptr [rdx - 2], r9w
        add       rcx, 2
        sub       rdx, 2
        jmp       rmid
rdone:
        mov       rax, r8
        ret
wia_wcsrev ENDP
END
