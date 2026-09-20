; changes/125-rtlfindclearbits/impl.asm
; Ulong wia_findclearbits(const RTL_BITMAP* bm, ulong NumberToFind, ulong HintIndex)  [rcx,rdx,r8 -> eax]
;
; Reimplements ntdll!RtlFindClearBits: find the first run of `NumberToFind` consecutive clear (0) bits,
; searching cyclically from HintIndex (wrapping to 0), returning the run's start bit index, or 0xFFFFFFFF.
; Contract (validated bit-exact vs live over 5M fuzz): let n = SizeOfBitMap, h = (hint>=n?0:hint);
;   NumberToFind == 0  -> return h & ~7  (byte-floored clamped hint)
;   NumberToFind >  n  -> return 0xFFFFFFFF
;   else: over valid window starts [0, n-num], scan cyclically beginning at (h if h<=n-num else 0),
;         return the first start p whose window [p,p+num) is all clear; else 0xFFFFFFFF.
;
; ntdll uses a per-byte run-length table. This scans 64-bit words: it extends a clear-run across word
; boundaries (returning the moment it reaches num) and tests each word for an interior run >= num in
; O(1)/O(log num), a POPCNT reject then, when num<=popcount<=64, an O(log num) shift-reduce whose set
; bits mark run>=num starts (tzcnt = earliest). An AVX2 fast path bulk-skips 256-bit chunks that hold no
; target bit (all-set for clear-search), the common long allocated/free runs on real bitmaps. Pass 1
; scans [startpos, n); if nothing, pass 2 scans [0, startpos+num) (so total work ~= n, not 2n).
; Unified with RtlFindSetBits (126) by an invert mask (r14): clear-search 0, set-search -1.
;
; ISA: AVX2 + POPCNT + LZCNT/TZCNT. Validated on Zen3.
;   frame: [rsp]=word  [rsp+8]=scan limit  [rsp+16]=startpos  [rsp+24]=pass flag

; Register note. This function may only touch xmm0-xmm5: xmm6-xmm15 are callee-saved under Win64
; (their low 128 bits are; the upper halves are volatile). An earlier cut kept the all-ones vector
; and the broadcast invert mask in ymm7/ymm6, which silently destroyed any double the caller had
; live, invisible to a correctness test, which compares a bit index. Only registers 0 and 1 were
; otherwise in use, so moving the pair to ymm3/ymm2 costs nothing. See tools/abi-check.
;
.code
wia_findclearbits PROC
        push      rbx
        push      rsi
        push      rdi
        push      rbp
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 32
        mov       r9d, dword ptr [rcx]            ; n
        mov       rsi, [rcx + 8]                  ; buf
        mov       rdi, rdx                        ; num
        xor       r14d, r14d                      ; invert = 0 (clear-search)  [126 sets -1]

        mov       eax, r8d                        ; hint
        cmp       eax, r9d
        jb        h_ok
        xor       eax, eax                         ; h = (hint>=n)?0:hint
h_ok:
        test      rdi, rdi
        jnz       num_pos
        and       eax, -8                          ; num==0 -> h & ~7
        jmp       epi
num_pos:
        cmp       rdi, r9                          ; num > n -> -1
        ja        ret_nf
        mov       r10, r9
        sub       r10, rdi                         ; n - num
        cmp       eax, r10d                        ; h <= n-num ?
        jbe       sp_ok
        xor       eax, eax                         ; startpos = 0
sp_ok:
        mov       r13d, eax                        ; startpos
        mov       [rsp + 16], r13
        ; ymm3 = all ones; ymm2 = broadcast(invert) so (chunk XOR ymm2) has target bits = 0
        vpcmpeqd  ymm3, ymm3, ymm3
        vmovq     xmm2, r14
        vpbroadcastq ymm2, xmm2
        mov       [rsp + 8], r9                    ; pass 1 limit = n
        mov       byte ptr [rsp + 24], 1
        mov       r8, r13                          ; from = startpos
scan_pass:
        xor       ebx, ebx                         ; run = 0
sf_loop:
        cmp       r8, [rsp + 8]                    ; cur >= limit ?
        jae       sf_none
        ; ---- AVX2 skip: aligned, >=256 valid bits, chunk has no target bit -> advance, run resets ----
        test      r8d, 63
        jnz       sf_word
        lea       rax, [r8 + 256]
        cmp       rax, r9
        ja        sf_word
        mov       rdx, r8
        shr       rdx, 3
        vmovdqu   ymm0, ymmword ptr [rsi + rdx]
        vpxor     ymm0, ymm0, ymm2                 ; target bits -> 0
        vpxor     ymm1, ymm0, ymm3                 ; ~ (no-target => all ones => this is 0)
        vptest    ymm1, ymm1
        jnz       sf_word                          ; some target bit present -> handle word-by-word
        add       r8, 256                          ; all non-target: skip, run broken
        xor       ebx, ebx
        jmp       sf_loop
sf_word:
        mov       r10, r8
        and       r10, -64                         ; wordbase
        mov       r12, r8
        sub       r12, r10                         ; lowoff
        mov       rdx, r10
        shr       rdx, 3
        mov       rax, [rsi + rdx]
        xor       rax, r14                          ; invert for set-search
        mov       ecx, r12d
        shr       rax, cl                           ; bit0 := cur
        mov       r11, 64
        sub       r11, r12                          ; 64 - lowoff
        mov       r15, r9
        sub       r15, r8                           ; n - cur
        cmp       r11, r15
        jbe       vb_ok
        mov       r11, r15                          ; validbits = min(64-lowoff, n-cur)
vb_ok:
        cmp       r11, 64
        jae       vb_nomask
        mov       ecx, r11d
        mov       rdx, -1
        shl       rdx, cl
        or        rax, rdx                          ; force bits >= validbits to non-target
vb_nomask:
        mov       r15, r8                           ; base = cur
        lea       r8, [r10 + 64]                    ; next cur (word-aligned)
        mov       [rsp], rax                        ; save word
        tzcnt     rcx, rax                          ; lo = leading clear run
        cmp       rcx, r11
        jb        sf_mixed
        test      rbx, rbx                          ; valid region all clear -> extend, continue
        jnz       sf_a0_ext
        mov       rbp, r15
sf_a0_ext:
        add       rbx, r11
        cmp       rbx, rdi
        jae       sf_found_rs
        jmp       sf_loop
sf_mixed:
        test      rbx, rbx
        jnz       sf_m_ext
        mov       rbp, r15
sf_m_ext:
        add       rbx, rcx                          ; run += lo
        cmp       rbx, rdi
        jae       sf_found_rs
        mov       r10, rax                          ; t = clear bits in valid region
        not       r10
        cmp       r11, 64
        jae       sf_tfull
        mov       ecx, r11d
        mov       rdx, 1
        shl       rdx, cl
        dec       rdx
        and       r10, rdx
sf_tfull:
        popcnt    rdx, r10
        cmp       rdx, rdi
        jb        sf_hi                             ; < num clear bits -> no interior run
        mov       r12, r10                          ; s = t
        mov       rax, rdi
        dec       rax                               ; rem = num-1
        mov       rdx, 1                            ; acc
sf_red:
        test      rax, rax
        jz        sf_red_done
        mov       rcx, rdx
        cmp       rcx, rax
        jbe       sf_red_sh
        mov       rcx, rax
sf_red_sh:
        sub       rax, rcx
        add       rdx, rdx
        mov       r10, r12
        shr       r10, cl
        and       r12, r10
        jmp       sf_red
sf_red_done:
        test      r12, r12
        jz        sf_hi
        tzcnt     rcx, r12
        add       ecx, r15d
        mov       eax, ecx
        jmp       epi
sf_hi:
        mov       rax, [rsp]
        mov       ecx, 64
        sub       ecx, r11d
        shl       rax, cl
        lzcnt     rcx, rax                          ; hi = trailing clear run of valid region
        mov       rbx, rcx
        mov       rbp, r15
        add       rbp, r11
        sub       rbp, rcx
        cmp       rbx, rdi
        jae       sf_found_rs
        jmp       sf_loop
sf_found_rs:
        mov       eax, ebp
        jmp       epi
sf_none:
        cmp       byte ptr [rsp + 24], 1            ; was this pass 1 ?
        jne       ret_nf
        test      r13, r13                          ; startpos 0 -> pass 1 already covered all
        jz        ret_nf
        mov       byte ptr [rsp + 24], 2
        mov       rax, r13                          ; limit2 = min(n, startpos+num)
        add       rax, rdi
        cmp       rax, r9
        jbe       lim2_ok
        mov       rax, r9
lim2_ok:
        mov       [rsp + 8], rax
        xor       r8d, r8d
        jmp       scan_pass
ret_nf:
        mov       eax, -1
epi:
        vzeroupper
        add       rsp, 32
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rbp
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_findclearbits ENDP
END
