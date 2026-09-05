; changes/130-rtlsetbits/impl.asm
; VOID wia_setbits(RTL_BITMAP* bm, ULONG StartingIndex, ULONG NumberToSet)   [Win64: rcx, edx, r8d]
;
; Reimplements ntdll!RtlSetBits: set bits [StartingIndex, StartingIndex+NumberToSet) to 1.
; Contract (probed against the live export): there is NO bounds check whatsoever -- ntdll writes past
; SizeOfBitMap if asked (start=250,num=20 on a 256-bit map sets bits 250..269; start=300 sets 300..309),
; and NumberToSet == 0 is a no-op. So SizeOfBitMap is never read.
;
; ntdll costs ~3.8 ns even to set a single word (~17 cycles of fixed overhead) while its bulk fill is
; already wide, so the win here is the small/medium range. Edges are masked at ULONG granularity --
; matching the buffer's declared element type, so no byte outside the ULONG array is ever touched (a
; 64-bit read-modify-write could fault on a buffer ending exactly at a page boundary) -- and the
; interior is filled 32 bytes at a time with AVX2.
;
; ISA: AVX2. Validated on Zen3.

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
        ; Bulk fill, three regimes -- each measured (see RESULTS.md): a plain store loop for tiny
        ; runs, an UNROLLED 128-byte AVX2 loop for the middle (rep stos has too much startup there:
        ; 512 bytes cost 37 ns via rep vs 6 ns unrolled), and rep stosd only once it is large enough
        ; for fast-short-rep to win on streaming bandwidth.
        test      r11, r11
        jz        sb_tail
        cmp       r11, 4096
        jae       sb_rep
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
        push      rdi
        mov       rdi, r9
        mov       rcx, r11
        mov       eax, -1
        rep       stosd
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
