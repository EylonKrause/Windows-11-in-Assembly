; changes/130-rtlsetbits/impl_tgl.asm
; Void wia_setbits(RTL_BITMAP* bm, ulong StartingIndex, ulong NumberToSet)   [Win64: rcx, edx, r8d]
;
; Reimplements ntdll!RtlSetBits: set bits [StartingIndex, StartingIndex+NumberToSet) to 1.
; Contract (probed against the live export): there is NO bounds check whatsoever, ntdll writes past
; SizeOfBitMap if asked (start=250,num=20 on a 256-bit map sets bits 250..269; start=300 sets 300..309),
; and NumberToSet == 0 is a no-op. So SizeOfBitMap is never read.
;
; ntdll costs ~3.8 ns even to set a single word (~17 cycles of fixed overhead) while its bulk fill is
; already wide, so the win here is the small/medium range. Edges are masked at ULONG granularity --
; matching the buffer's declared element type, so no byte outside the ULONG array is ever touched (a
; 64-bit read-modify-write could fault on a buffer ending exactly at a page boundary), and the
; interior is filled 32 bytes at a time with AVX2.
;
; ISA: AVX2, plus ERMS/FSRM `rep stosb`. Validated on bench #3 (Intel i9-11900H, Tiger Lake-H)
; see docs/PLATFORM-i9-11900H.md.
;
; Tiger lake variant of change 130. Everything below is the parent's code except two things in the
; bulk fill, and the two things it is NOT are worth recording because both were tried and measured.
;
; What is wrong on this part
;   * The parent does not reach `rep` until 4096 ULONGs (16 KB), because on Zen 3 rep startup is
;     brutal, its own comment records 512 bytes costing 37 ns via rep against 6 ns unrolled. This
;     part has ERMS/FSRM, and the parent's 40000-bit class measures 0.55x here: 69.68 ns against
;     ntdll's 38.38, while ntdll moves 130 GB/s, which is more than a 32-byte-store loop can do.
;   * The parent uses `rep STOSD`. ERMS accelerates the BYTE form; the dword form does not get the
;     fast-string path at all. That is why even the parent's largest class (32 KB) sits at 0.86x
;     inside the branch that was supposed to be its fastest.
;
; Two things tried that made it worse, kept here so they are not tried again:
;   * rep from 64 ULONGs (256 bytes), on the reasoning that FSRM means "fast SHORT rep". It fixed
;     40000 (0.55x -> 0.91x) and BROKE 4096: 512 bytes cost 18.61 ns through rep against 7.86 ns
;     through stores. Fast-short-rep is faster than the old rep, not free; there is still roughly
;     15 ns of startup.
;   * 512-bit stores for the middle rung. At 512 bytes that measured 13.17 ns against the parent's
;     7.86, eight stores are not enough to amortise the AVX-512 transition this part pays when
;     zmm has not been used recently. The parent's 128-byte-unrolled SSE/AVX2 rungs are kept
;     unchanged for exactly that reason.
;
; So the variant changes the THRESHOLD and the INSTRUCTION, and nothing else. From the two measured
; points (stores at about 100 GB/s, rep streaming about 200 GB/s after startup) the crossover is
; near 3 KB, which is 768 ULONGs.
;
; Filling ULONGs byte-wise is exact here only because the fill value is all-ones: every byte of
; 0xFFFFFFFF is 0xFF, so byte and dword granularity write identical memory. It would NOT be safe
; for a general fill value.

.code
wia_setbits PROC
        test      r8d, r8d
        jz        sb_done                         ; NumberToSet == 0 -> no-op
        mov       r9, [rcx + 8]                   ; Buffer (ULONG*)
        mov       eax, edx                        ; StartingIndex
        mov       r10d, r8d                       ; NumberToSet
        mov       rdx, rax
        shr       rdx, 5                          ; ULONG index
        lea       r9, [r9 + rdx*4]
        and       eax, 31                         ; bit offset within that ULONG
        jz        sb_aligned                      ; already ULONG-aligned: go straight to the bulk,
                                                  ; keeping the AVX2 loop aligned and the tail short
        mov       ecx, 32
        sub       ecx, eax                        ; bits available in the first ULONG (1..32)
        cmp       r10, rcx
        jae       sb_full1
        ; ---- whole range fits inside the first ULONG ----
        mov       edx, 1
        mov       ecx, r10d
        shl       edx, cl
        dec       edx                             ; (1 << num) - 1
        mov       ecx, eax
        shl       edx, cl                         ; << offset
        or        [r9], edx
        ret
sb_full1:
        ; ---- set the top of the first ULONG, then whole ULONGs, then the tail ----
        mov       edx, -1
        mov       r11d, ecx                       ; avail
        mov       ecx, eax
        shl       edx, cl                         ; ~0 << offset
        or        [r9], edx
        add       r9, 4
        sub       r10, r11                        ; remaining bits (now ULONG-aligned)
sb_aligned:
        mov       r11, r10
        shr       r11, 5                          ; whole ULONGs to fill
        ; Bulk fill, three regimes, each measured (see RESULTS.md): a plain store loop for tiny
        ; runs, an UNROLLED 128-byte AVX2 loop for the middle (rep stos has too much startup there:
        ; 512 bytes cost 37 ns via rep vs 6 ns unrolled), and rep stosd only once it is large enough
        ; for fast-short-rep to win on streaming bandwidth.
        test      r11, r11
        jz        sb_tail
        cmp       r11, 768                        ; 3 KB: measured crossover for ERMS on this part,
        jae       sb_rep                          ; against 4096 ULONGs (16 KB) on Zen 3
        cmp       r11, 256
        jae       sb_avx                          ; 1 KB+ : 32-byte stores win despite vzeroupper
        cmp       r11, 8
        jb        sb_l4
        ; SSE2, not AVX2: Zen3 retires two 16-byte stores per cycle, so this matches 32-byte-store
        ; throughput while avoiding the vzeroupper / AVX-transition cost, which measurably dominated
        ; at the 512-byte size (10.0 ns with AVX2+vzeroupper vs the numbers in RESULTS.md).
        pcmpeqd   xmm0, xmm0                      ; all ones
        cmp       r11, 64
        jb        sb_chk128
sb_a256:
        movdqu    xmmword ptr [r9], xmm0
        movdqu    xmmword ptr [r9 + 16], xmm0
        movdqu    xmmword ptr [r9 + 32], xmm0
        movdqu    xmmword ptr [r9 + 48], xmm0
        movdqu    xmmword ptr [r9 + 64], xmm0
        movdqu    xmmword ptr [r9 + 80], xmm0
        movdqu    xmmword ptr [r9 + 96], xmm0
        movdqu    xmmword ptr [r9 + 112], xmm0
        movdqu    xmmword ptr [r9 + 128], xmm0
        movdqu    xmmword ptr [r9 + 144], xmm0
        movdqu    xmmword ptr [r9 + 160], xmm0
        movdqu    xmmword ptr [r9 + 176], xmm0
        movdqu    xmmword ptr [r9 + 192], xmm0
        movdqu    xmmword ptr [r9 + 208], xmm0
        movdqu    xmmword ptr [r9 + 224], xmm0
        movdqu    xmmword ptr [r9 + 240], xmm0
        add       r9, 256
        sub       r11, 64
        cmp       r11, 64
        jae       sb_a256
sb_chk128:
        cmp       r11, 32
        jb        sb_a16
sb_a128:
        movdqu    xmmword ptr [r9], xmm0
        movdqu    xmmword ptr [r9 + 16], xmm0
        movdqu    xmmword ptr [r9 + 32], xmm0
        movdqu    xmmword ptr [r9 + 48], xmm0
        movdqu    xmmword ptr [r9 + 64], xmm0
        movdqu    xmmword ptr [r9 + 80], xmm0
        movdqu    xmmword ptr [r9 + 96], xmm0
        movdqu    xmmword ptr [r9 + 112], xmm0
        add       r9, 128
        sub       r11, 32
        cmp       r11, 32
        jae       sb_a128
sb_a16:
        test      r11, r11                        ; the 128-byte loop can leave exactly 0 ULONGs;
        jz        sb_tail                         ; sb_l4 is a dec/jnz loop and needs r11 >= 1
        cmp       r11, 4
        jb        sb_l4
sb_a16l:
        movdqu    xmmword ptr [r9], xmm0
        add       r9, 16
        sub       r11, 4
        cmp       r11, 4
        jae       sb_a16l
        test      r11, r11
        jz        sb_tail
        jmp       sb_l4                           ; must NOT fall through into sb_avx below
sb_avx:
        vpcmpeqd  ymm0, ymm0, ymm0
sb_v128:
        vmovdqu   ymmword ptr [r9], ymm0
        vmovdqu   ymmword ptr [r9 + 32], ymm0
        vmovdqu   ymmword ptr [r9 + 64], ymm0
        vmovdqu   ymmword ptr [r9 + 96], ymm0
        add       r9, 128
        sub       r11, 32
        cmp       r11, 32
        jae       sb_v128
        vzeroupper
        test      r11, r11
        jz        sb_tail
        jmp       sb_l4
sb_rep:
        push      rdi                             ; rdi is non-volatile under Win64
        mov       rdi, r9
        mov       rcx, r11
        shl       rcx, 2                          ; ULONGs -> BYTES

        ; Align the destination to a cache line first. This was removed once, on a single
        ; comparison that showed it slower at 32 KB, and putting it back is the result of reading
        ; the distribution instead of one number.
        ;
        ; Without it the 5000-byte class is BIMODAL: the same binary measured 37.2 ns four times in
        ; a row and then 53.9-55.4 ns six times in a row, while ntdll's figure for the same class
        ; never left 36-38 ns. A swing that large in ours alone, with the comparand steady, is not
        ; ambient load, load would move both. `rep stosb` is sensitive to the alignment of its
        ; destination, and the harness's buffer comes from an allocation whose alignment is not
        ; fixed from run to run. That is exactly what a bimodal distribution with a stable
        ; comparand looks like.
        ;
        ; So the head is filled with two unaligned 32-byte stores and the rep starts on a 64-byte
        ; boundary. This branch is entered only at 768 ULONGs (3072 bytes) or more, so a 64-byte
        ; store cannot overrun.
        vpcmpeqd  ymm0, ymm0, ymm0
        vmovdqu   ymmword ptr [rdi], ymm0
        vmovdqu   ymmword ptr [rdi + 32], ymm0
        vzeroupper
        mov       rax, rdi
        add       rdi, 64
        and       rdi, -64                        ; next 64-byte boundary
        sub       rax, rdi                        ; = -(bytes the store already covered)
        add       rcx, rax                        ; so rcx -= that

        mov       eax, -1                         ; al = 0FFh
        rep       stosb                           ; STOSB, not STOSD: this is the ERMS fast path
        mov       r9, rdi
        pop       rdi
        jmp       sb_tail
sb_l4:
        mov       dword ptr [r9], -1
        add       r9, 4
        dec       r11
        jnz       sb_l4
sb_tail:
        and       r10, 31                         ; leftover bits in the final ULONG
        jz        sb_done
        mov       edx, 1
        mov       ecx, r10d
        shl       edx, cl
        dec       edx
        or        [r9], edx
sb_done:
        ret
wia_setbits ENDP
END
