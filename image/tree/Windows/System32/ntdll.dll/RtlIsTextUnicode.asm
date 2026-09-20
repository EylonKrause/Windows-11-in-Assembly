; ntdll.dll!RtlIsTextUnicode  --  hand-written x86-64 reimplementation (5.71x vs shipped)
; source of truth: changes/193-rtlistextunicode/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/193-rtlistextunicode/impl.asm
; BOOLEAN wia_istextunicode(const void* buf, int len, int* lpi)   [rcx, edx, r8 -> al]
;
; Reimplements ntdll!RtlIsTextUnicode -- the slowest routine found anywhere in this project's
; headroom survey: 719 ns for a 508-byte buffer, about 2.9 ns PER 16-BIT UNIT, where everything
; else here that scans memory runs at 20-80 GB/s.
;
; Why it is worth converting at all -- and this had to be measured before any work started:
;   the shipped cost SATURATES. 16 B -> 56 ns, 256 B -> 368 ns, 508 B -> 719 ns, and then FLAT at
;   ~735 ns from 1 KB all the way to 128 KB. The disassembly says why: "mov r14d,100h ; cmova
;   edx,r14d" clamps the unit count to 256, so ntdll never inspects more than 512 bytes. That
;   means a vectorised version wins on every size at or above the cap, not just on small buffers.
;
; The contract is in reference.c. It could not be derived black-box -- three probe rounds failed to
; explain ASCII16 and STATISTICS -- so it was read out of the shipped code (dumpbin /disasm,
; RVA 0x000D3A10) and then fuzz-confirmed: 3 000 000 cases, 0 mismatches, plus every buffer of
; length 2..6 over the alphabet {00,09,0A,0D,1A,20,30,61,FE,FF} exhaustively.
;
; The key to vectorising it: both statistics are total variation sums,
;     lo_var = sum |b[2i]   - b[2i-2]| ,  hi_var = sum |b[2i+1] - b[2i-1]|
; i.e. both are |b[j] - b[j-2]| over the same byte stream, split by the parity of j. So ONE
; unaligned load at cursor-2 gives every predecessor at once, vpmaxub/vpminub/vpsubb gives the
; absolute differences without a single branch, and two masked vpsadbw's split and sum them 32
; bytes at a time. No cross-iteration dependency exists, because the predecessor comes from memory
; rather than from a carried register.
; The CR/LF counter is the same shape one byte over: it pairs b[2i] with b[2i-1], so a second
; unaligned load at cursor-1 turns it into two vpcmpeqb pairs and a popcnt.
; The thirteen "is this exact unit present" tests become 13 vpcmpeqw accumulated into three ymm
; OR-registers, which is why presence -- not counts -- is all that is needed: every one of
; CONTROLS / REVERSE_CONTROLS / ILLEGAL_CHARS is a non-zero test over its group.
;
; Everything lives in ymm0-ymm5, the volatile half of the register file, so the function needs
; no ymm spill and no stack frame at all. That is not cosmetic: the first cut spilled ymm6-ymm9
; into a 168-byte frame and measured 0.82x on a 16-byte buffer from the fixed cost alone.
;
; ISA: AVX2 + POPCNT. No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
zerov   db 32 dup(0)
evenm   dw 16 dup(00FFh)              ; keeps the bytes at EVEN offsets = the low bytes
oddm    dw 16 dup(0FF00h)             ; keeps the bytes at ODD offsets  = the high bytes
b0D     db 32 dup(0Dh)
b0A     db 32 dup(0Ah)
w0020   dw 16 dup(0020h)
w0009   dw 16 dup(0009h)
w000A   dw 16 dup(000Ah)
w000D   dw 16 dup(000Dh)
w3000   dw 16 dup(3000h)
w0900   dw 16 dup(0900h)
w0A00   dw 16 dup(0A00h)
w0D00   dw 16 dup(0D00h)
w2000   dw 16 dup(2000h)
w0A0D   dw 16 dup(0A0Dh)
wFFFE   dw 16 dup(0FFFEh)
wFFFF   dw 16 dup(0FFFFh)

.code
wia_istextunicode PROC
        push      rbx
        push      rbp
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        ; No stack frame and no ymm6-ymm9 spill: this function makes no ABI call, so it has no
        ; alignment obligation, and everything below lives in ymm0-ymm5, the volatile half. The
        ; first cut saved four ymm registers into a 168-byte frame and measured 0.82x on a 16-byte
        ; buffer -- a regression that parks the change -- purely from that fixed cost.

        mov       rsi, rcx                     ; buf
        mov       edi, edx                     ; len
        mov       rbp, r8                      ; lpi

        ;================ n = min(len/2, 256), with the early exits ================
        mov       r9d, edi
        shr       r9d, 1                       ; n_all
        mov       eax, 256
        cmp       r9d, eax
        mov       r8d, r9d                     ; keep n_all
        cmova     r9d, eax                     ; n
        test      r9d, r9d
        jz        ret_five

        cmp       edi, 2
        je        len_two
        ja        len_big
        jmp       have_n                       ; len < 2 cannot reach here (n would be 0)
len_two:
        movzx     eax, word ptr [rsi]
        test      ax, ax
        jz        have_n                       ; a zero unit is NOT the early-exit case
        test      ax, 0FF00h
        jz        ret_five                     ; nonzero with a zero high byte -> *lpi = 5, FALSE
        jmp       have_n
len_big:
        cmp       r8d, 256                     ; the trailing-unit trim only applies when the
        ja        have_n                       ; WHOLE buffer is at most 256 units
        test      dil, 1
        jnz       have_n                       ; and only for an even length
        mov       eax, r9d
        dec       eax
        movzx     eax, word ptr [rsi + rax*2]  ; the last unit
        test      ax, 0FF00h
        jnz       have_n
        dec       r9d                          ; zero high byte -> drop it from the scan
have_n:

        ;================ accumulators ================
        xor       r12d, r12d                   ; zero-byte count
        xor       r13d, r13d                   ; crlf count
        xor       r14d, r14d                   ; CONTROLS presence   (scalar half)
        xor       r15d, r15d                   ; REVERSE_CONTROLS presence
        xor       ebx, ebx                     ; ILLEGAL presence
        xor       ecx, ecx                     ; lo_var
        xor       edx, edx                     ; hi_var
        vpxor     xmm4, xmm4, xmm4             ; lo_var vector accumulator
        vpxor     xmm5, xmm5, xmm5             ; hi_var vector accumulator

        ;================ unit 0, scalar: prev_lo = prev_hi = 0 ================
        movzx     eax, byte ptr [rsi]          ; lo0
        movzx     r8d, byte ptr [rsi+1]        ; hi0
        mov       ecx, eax                     ; lo_var = |lo0 - 0|
        mov       edx, r8d                     ; hi_var = |hi0 - 0|
        test      eax, eax
        jnz       u0_l
        inc       r12d
u0_l:
        test      r8d, r8d
        jnz       u0_h
        inc       r12d
u0_h:
        ; no crlf from unit 0: prev_hi is 0, which is neither 0x0A nor 0x0D
        shl       r8d, 8
        or        r8d, eax                     ; r8d = unit 0
        call      presence_scalar

        ;================ the vector body: units 1.. ================
        lea       r10, [rsi + 2]               ; cursor, byte offset 2
        lea       r11, [rsi + r9*2]            ; end
        mov       rax, r11
        sub       rax, r10
        cmp       rax, 32
        jb        tail                         ; fewer than 16 units left
vloop:
        vmovdqu   ymm0, ymmword ptr [r10]      ; A = b[j..j+31]
        vmovdqu   ymm1, ymmword ptr [r10-2]    ; B = b[j-2..j+29]   (the predecessors)
        vpmaxub   ymm2, ymm0, ymm1
        vpminub   ymm3, ymm0, ymm1
        vpsubb    ymm2, ymm2, ymm3             ; |A - B| bytewise, branchless
        vpand     ymm3, ymm2, ymmword ptr [evenm]
        vpsadbw   ymm3, ymm3, ymmword ptr [zerov]
        vpaddq    ymm4, ymm4, ymm3             ; low-byte differences
        vpand     ymm2, ymm2, ymmword ptr [oddm]
        vpsadbw   ymm2, ymm2, ymmword ptr [zerov]
        vpaddq    ymm5, ymm5, ymm2             ; high-byte differences

        vpcmpeqb  ymm2, ymm0, ymmword ptr [zerov]
        vpmovmskb eax, ymm2
        popcnt    eax, eax
        add       r12d, eax                    ; zero bytes

        vmovdqu   ymm1, ymmword ptr [r10-1]    ; C = b[j-1..]  -> the CR/LF partner
        vpcmpeqb  ymm2, ymm0, ymmword ptr [b0D]
        vpcmpeqb  ymm3, ymm1, ymmword ptr [b0A]
        vpand     ymm2, ymm2, ymm3
        vpcmpeqb  ymm3, ymm0, ymmword ptr [b0A]
        vpcmpeqb  ymm1, ymm1, ymmword ptr [b0D]
        vpand     ymm3, ymm3, ymm1
        vpor      ymm2, ymm2, ymm3
        vpand     ymm2, ymm2, ymmword ptr [evenm]   ; only low-byte positions count
        vpmovmskb eax, ymm2
        popcnt    eax, eax
        add       r13d, eax

        vpcmpeqw  ymm2, ymm0, ymmword ptr [w0020]
        vpcmpeqw  ymm3, ymm0, ymmword ptr [w0009]
        vpor      ymm2, ymm2, ymm3
        vpcmpeqw  ymm3, ymm0, ymmword ptr [w000A]
        vpor      ymm2, ymm2, ymm3
        vpcmpeqw  ymm3, ymm0, ymmword ptr [w000D]
        vpor      ymm2, ymm2, ymm3
        vpcmpeqw  ymm3, ymm0, ymmword ptr [w3000]
        vpor      ymm2, ymm2, ymm3
        vpmovmskb eax, ymm2
        or        r14d, eax

        vpcmpeqw  ymm2, ymm0, ymmword ptr [w0900]
        vpcmpeqw  ymm3, ymm0, ymmword ptr [w0A00]
        vpor      ymm2, ymm2, ymm3
        vpcmpeqw  ymm3, ymm0, ymmword ptr [w0D00]
        vpor      ymm2, ymm2, ymm3
        vpcmpeqw  ymm3, ymm0, ymmword ptr [w2000]
        vpor      ymm2, ymm2, ymm3
        vpmovmskb eax, ymm2
        or        r15d, eax

        vpcmpeqw  ymm2, ymm0, ymmword ptr [zerov]
        vpcmpeqw  ymm3, ymm0, ymmword ptr [w0A0D]
        vpor      ymm2, ymm2, ymm3
        vpcmpeqw  ymm3, ymm0, ymmword ptr [wFFFE]
        vpor      ymm2, ymm2, ymm3
        vpcmpeqw  ymm3, ymm0, ymmword ptr [wFFFF]
        vpor      ymm2, ymm2, ymm3
        vpmovmskb eax, ymm2
        or        ebx, eax

        add       r10, 32
        mov       rax, r11
        sub       rax, r10
        cmp       rax, 32
        jae       vloop

        ;---- fold the vector accumulators into ecx (lo_var) and edx (hi_var) ----
        vextracti128 xmm2, ymm4, 1
        vpaddq    xmm4, xmm4, xmm2
        vpextrq   rax, xmm4, 0
        add       rcx, rax
        vpextrq   rax, xmm4, 1
        add       rcx, rax
        vextracti128 xmm2, ymm5, 1
        vpaddq    xmm5, xmm5, xmm2
        vpextrq   rax, xmm5, 0
        add       rdx, rax
        vpextrq   rax, xmm5, 1
        add       rdx, rax
        ;================ scalar tail: the remaining units ================
tail:
        cmp       r10, r11
        jae       scan_done
        movzx     eax, byte ptr [r10]          ; lo
        movzx     r8d, byte ptr [r10+1]        ; hi
        test      eax, eax
        jnz       t_l
        inc       r12d
t_l:
        test      r8d, r8d
        jnz       t_h
        inc       r12d
t_h:
        movzx     r9d, byte ptr [r10-1]        ; the previous unit's HIGH byte
        cmp       eax, 0Dh
        jne       t_c1
        cmp       r9d, 0Ah
        jne       t_c2
        inc       r13d
        jmp       t_c2
t_c1:
        cmp       eax, 0Ah
        jne       t_c2
        cmp       r9d, 0Dh
        jne       t_c2
        inc       r13d
t_c2:
        movzx     r9d, byte ptr [r10-2]        ; previous low byte
        sub       r9d, eax
        mov       eax, r9d
        sar       eax, 31
        xor       r9d, eax
        sub       r9d, eax                     ; |prev_lo - lo|
        add       rcx, r9
        movzx     r9d, byte ptr [r10-1]
        sub       r9d, r8d
        mov       eax, r9d
        sar       eax, 31
        xor       r9d, eax
        sub       r9d, eax                     ; |prev_hi - hi|
        add       rdx, r9
        movzx     eax, byte ptr [r10]
        shl       r8d, 8
        or        r8d, eax                     ; the unit
        call      presence_scalar
        add       r10, 2
        jmp       tail
scan_done:

        ;================ the post-loop CR/LF test and the zero-count adjustment ================
        movzx     eax, byte ptr [r11-2]        ; last_lo
        movzx     r8d, byte ptr [r11-1]        ; last_hi
        cmp       eax, 0Dh
        jne       e_c1
        cmp       r8d, 0Ah
        jne       e_c2
        inc       r13d
        jmp       e_c2
e_c1:
        cmp       eax, 0Ah
        jne       e_c2
        cmp       r8d, 0Dh
        jne       e_c2
        inc       r13d
e_c2:
        test      r8d, r8d
        jnz       e_hi_nz
        dec       r12d                         ; last_hi == 0 -> zero count minus one
        jmp       e_zc
e_hi_nz:
        cmp       r8d, 1Ah
        jne       e_zc
        inc       r13d                         ; a trailing Ctrl-Z counts as a CR/LF event
e_zc:

        ;================ assemble the flags ================
        xor       eax, eax                     ; f
        cmp       rcx, 7Fh
        jae       f_stats
        test      rdx, rdx
        jnz       f_rev16
        mov       eax, 1                       ; ASCII16
        jmp       f_stats
f_rev16:
        test      rcx, rcx
        jnz       f_stats
        mov       eax, 10h                     ; REVERSE_ASCII16
f_stats:
        lea       r9, [rdx + rdx*2]            ; 3 * hi_var
        cmp       r9, rcx
        jae       f_rstats
        or        eax, 2                       ; STATISTICS
f_rstats:
        lea       r9, [rcx + rcx*2]            ; 3 * lo_var
        cmp       r9, rdx
        jae       f_ctl
        or        eax, 20h                     ; REVERSE_STATISTICS
f_ctl:
        test      r14d, r14d
        jz        f_rctl
        or        eax, 4
f_rctl:
        test      r15d, r15d
        jz        f_ill
        or        eax, 40h
f_ill:
        test      ebx, ebx
        jnz       f_set_ill
        test      r13d, r13d
        jz        f_odd                        ; no CR/LF events at all
        mov       r9d, edi
        cmp       r9d, 512
        jbe       f_cap
        mov       r9d, 512
f_cap:
        mov       r8d, 0CCCCCCCDh              ; the shipped code's magic constant, with a TOTAL
        imul      r8, r9                       ; shift of 37 -- that is /40, NOT /10. Reading it
        shr       r8, 37                       ; as /10 left 8578 mismatches of 3 000 000.
        cmp       r13d, r8d
        jb        f_odd
f_set_ill:
        or        eax, 100h
f_odd:
        test      dil, 1
        jz        f_null
        or        eax, 200h
f_null:
        test      r12d, r12d
        jz        f_sig
        or        eax, 1000h
f_sig:
        movzx     r8d, word ptr [rsi]          ; the FIRST unit
        cmp       r8d, 0FEFFh
        jne       f_sig2
        or        eax, 8
        jmp       f_mask
f_sig2:
        cmp       r8d, 0FFFEh
        jne       f_mask
        or        eax, 80h

        ;================ mask by what the caller asked for, then decide ================
f_mask:
        test      rbp, rbp
        jz        decide
        and       dword ptr [rbp], eax
        mov       eax, dword ptr [rbp]
decide:
        mov       r8d, eax
        and       r8d, 0B08h
        cmp       r8d, 8
        je        ret_true
        test      eax, 0F0h
        jnz       ret_false
        test      eax, 0F00h
        jnz       ret_false
        test      eax, 0F00Fh
        jnz       ret_true
ret_false:
        xor       eax, eax
        jmp       epilogue
ret_true:
        mov       eax, 1
        jmp       epilogue

ret_five:
        test      rbp, rbp
        jz        rf_noep
        mov       dword ptr [rbp], 5
rf_noep:
        xor       eax, eax
epilogue:
        vzeroupper
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbp
        pop       rbx
        ret

; ---------------------------------------------------------------------------
; presence_scalar -- internal. IN: r8d = the unit. Sets r14d / r15d / ebx if the unit belongs to
; the controls / REVERSE_CONTROLS / illegal group. Clobbers r8d and eax only.
; It must NOT touch r9: the caller holds the unit count there and computes the end pointer from it
; immediately after the first call. Using r9 as the scratch here sent the end pointer to
; rsi + 2*0x6100 and faulted on the very first two-byte buffer.
; Ordered so the common case (both bytes nonzero, not 0x0A0D, below 0xFFFE) costs four compares.
; ---------------------------------------------------------------------------
presence_scalar:
        mov       eax, r8d
        and       eax, 0FF00h
        jz        ps_hi_zero                   ; high byte == 0
        test      r8b, r8b
        jz        ps_lo_zero                   ; low byte == 0
        cmp       r8d, 0A0Dh
        je        ps_ill
        cmp       r8d, 0FFFEh
        jae       ps_ill
        ret
ps_hi_zero:
        cmp       r8d, 20h
        ja        ps_done
        je        ps_ctl
        test      r8d, r8d
        jz        ps_ill
        cmp       r8d, 9
        je        ps_ctl
        cmp       r8d, 0Ah
        je        ps_ctl
        cmp       r8d, 0Dh
        je        ps_ctl
ps_done:
        ret
ps_lo_zero:
        cmp       eax, 3000h
        je        ps_ctl
        cmp       eax, 0900h
        je        ps_rctl
        cmp       eax, 0A00h
        je        ps_rctl
        cmp       eax, 0D00h
        je        ps_rctl
        cmp       eax, 2000h
        je        ps_rctl
        ret
ps_ctl:
        or        r14d, 1
        ret
ps_rctl:
        or        r15d, 1
        ret
ps_ill:
        or        ebx, 1
        ret
wia_istextunicode ENDP
END
