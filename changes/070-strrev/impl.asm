; changes/070-strrev/impl.asm
; char* wia_strrev(char* s)   [Win64: rcx -> rax (returns s)]
;
; Reverse a NUL-terminated byte string in place. ucrtbase's is fully scalar: a byte-at-a-
; time strlen then a scalar two-pointer inward swap (no vectors, no page checks). That is
; near-optimal for SHORT strings -- a vector strlen can't amortise its ~10-cycle setup over
; 8 bytes -- so we match its strlen there (a scalar probe over the first 16 bytes, breaking
; at the NUL so it never reads past the terminator, exactly as page-safe as ucrtbase) but
; BEAT its reverse: instead of scalar swaps we byte-reverse whole registers with `bswap`
; (8-byte chunks) and 16-byte lanes with `vpshufb`, writing only bytes inside [lo,hi) (all
; owned). Tiers: >=32 B swap 16-byte vpshufb blocks from both ends; 16..31 B swap 8-byte
; bswap blocks; an exact 8-byte remainder is one bswap; the rest is scalar. Only when the
; string proves longer than 16 B do we switch to a page-safe 16-byte vector length scan.
;
; The whole routine uses ONLY volatile registers -> no push/pop to pay on a small string.
; ISA: AVX + SSSE3 (vpshufb). Validated bit-exact vs ucrtbase on Zen3.

.const
ALIGN 16
revm db 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0

.code
wia_strrev PROC
        mov       r8, rcx                            ; original s (return value)
        mov       rax, rcx                           ; scan ptr

        ; ---- unrolled scalar strlen probe: first 16 bytes. Independent lea+cmp+je per
        ; position (no serial pointer chain, no loop counter, one branch each). Each cmp
        ; only executes if the prior byte was non-zero, so it never *retires* a read past
        ; the NUL -- exactly as page-safe as ucrtbase's scalar strlen. ----
        lea       rax, [rcx]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 1]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 2]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 3]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 4]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 5]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 6]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 7]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 8]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 9]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 10]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 11]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 12]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 13]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 14]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 15]
        cmp       byte ptr [rax], 0
        je        sl_done

        ; ---- not terminated in 16 bytes: long string, page-safe vector scan from here ----
        lea       rax, [rcx + 16]
        vpxor     xmm1, xmm1, xmm1
slv:
        mov       r9, rax
        and       r9, 4095
        cmp       r9, 4080                           ; within 16 of a page end?
        ja        slv_scalar
        vmovdqu   xmm0, xmmword ptr [rax]
        vpcmpeqb  xmm0, xmm0, xmm1
        vpmovmskb r9d, xmm0
        test      r9d, r9d
        jnz       slv_found
        add       rax, 16
        jmp       slv
slv_scalar:
        cmp       byte ptr [rax], 0
        je        sl_done
        inc       rax
        jmp       slv
slv_found:
        bsf       r9d, r9d
        add       rax, r9
sl_done:
        ; rax -> NUL. lo = rcx (= s, untouched), hi = rax (one past last char).
        mov       rdx, rax                           ; hi

        ; ---- tier 1: 16-byte vpshufb block swaps (remaining >= 32) ----
        mov       rax, rdx
        sub       rax, rcx                           ; remaining
        cmp       rax, 32
        jb        blk8
        vmovdqa   xmm2, xmmword ptr [revm]
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

        ; ---- tier 2: 8-byte bswap block swaps (remaining in [16,31]) ----
blk8:
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 16
        jb        rem8
        mov       rax, qword ptr [rcx]               ; front 8
        mov       r9,  qword ptr [rdx - 8]           ; back 8
        bswap     rax
        bswap     r9
        mov       qword ptr [rcx], r9                ; front <- reverse(back)
        mov       qword ptr [rdx - 8], rax           ; back  <- reverse(front)
        add       rcx, 8
        sub       rdx, 8
        jmp       blk8

        ; ---- tier 3: exact 8-byte remainder -> single bswap (the common short case) ----
rem8:
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 8
        jne       rmid
        mov       rax, qword ptr [rcx]
        bswap     rax
        mov       qword ptr [rcx], rax
        jmp       rdone

        ; ---- tier 4: scalar two-pointer swap (remaining 1..7 or 9..15) ----
rmid:
        lea       rax, [rdx - 1]
        cmp       rcx, rax
        jae       rdone
        movzx     r9d, byte ptr [rcx]
        movzx     r10d, byte ptr [rdx - 1]
        mov       byte ptr [rcx], r10b
        mov       byte ptr [rdx - 1], r9b
        inc       rcx
        dec       rdx
        jmp       rmid
rdone:
        mov       rax, r8
        ret
wia_strrev ENDP
END
