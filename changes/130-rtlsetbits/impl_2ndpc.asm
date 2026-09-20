; changes/130-rtlsetbits/impl_2ndpc.asm
;==============================================================================
; 2ND PC VARIANT  --  AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445
;==============================================================================
; The original `impl.asm` is UNTOUCHED and remains the 5950X (Zen 3)
; implementation of record. This is an ADDITIONAL variant tuned for the second
; PC. Same exported symbol (`wia_setbits`), so this change's existing
; correctness.c and bench.c validate it unmodified -- build with build_2ndpc.bat.
;
; Why a 2ND-PC variant is needed
; ------------------------------
; This change was PARKED on the 5950X and is still parked here, on the bulk-fill
; classes:
;
;     size        ours ns   system ns   ratio   verdict
;     1bit           2.38        5.56   2.34x   BETTER
;     64             2.59        4.34   1.68x   BETTER
;     200            3.59        6.01   1.68x   BETTER
;     4096           6.29        5.59   0.89x   WORSE   <-- gate failure
;     40000         39.07       37.78   0.97x   WORSE
;     262144       218.59      215.03   0.98x   ~tie
;     geomean 1.331x => PARKED (a size class regressed)
;
; Stable across three repeat runs: 4096 gives 0.91x / 0.88x / 0.88x.
;
; Cause -- a ZEN-3-SPECIFIC tuning decision that inverts on zen 4
; The original dispatches the bulk fill by size and deliberately chooses SSE2
; over AVX2 for the middle range. Its own comment says why:
;
;     "SSE2, not AVX2: Zen3 retires two 16-byte stores per cycle, so this
;      matches 32-byte-store throughput while avoiding the vzeroupper /
;      AVX-transition cost"
;
; That is correct ON ZEN 3, where a 256-bit store is split into two 128-bit
; halves, so 2x16B and 1x32B per cycle are the same bandwidth and the 128-bit
; form avoids the vzeroupper. ZEN 4 widened the datapath: 256-bit stores are
; native, so the 32-byte form is no longer merely equal -- it is the wider one,
; and the Zen 3 reasoning inverts.
;
; 4096 bits is 128 ULONGs = 512 bytes, which lands in exactly that SSE2 window
; (>= 8 and < 256 ULONGs), so the failing class is filled 16 bytes at a time on
; a core that would do 32.
;
; The fix -- re-tune the two dispatch boundaries for this core
; Only the two dispatch thresholds change; every fill loop is byte-for-byte the
; original.
;
;     boundary                       Zen 3 (impl.asm)   Zen 4 (here)
;     -----------------------------  -----------------  -------------
;     rep stosd from                 4096 ULONGs (16K)  1024 ULONGs (4K)
;     AVX2 32-byte stores from        256 ULONGs (1K)     32 ULONGs (128B)
;
; The AVX2 loop consumes 32 ULONGs per iteration and is entered only when
; r11 >= 32, so lowering its threshold to exactly 32 is safe -- and 128 ULONGs
; divides evenly by 32, so the failing class leaves no scalar tail at all.
; The loop-internal comparisons (cmp r11,64 / 32 / 4 inside the unrolled bodies)
; are NOT touched; only the two dispatch tests are.
;
; Contract preserved exactly
;   * There is NO bounds check whatsoever -- ntdll writes past SizeOfBitMap if
;     asked (start=250,num=20 on a 256-bit map sets bits 250..269), so
;     SizeOfBitMap is never read. Unchanged here.
;   * NumberToSet == 0 is a no-op.
;   * Edges are masked at ULONG granularity, matching the buffer's declared
;     element type, so no byte outside the ULONG array is ever touched -- a
;     64-bit read-modify-write could fault on a buffer ending exactly at a page
;     boundary. Unchanged here: only the interior bulk fill was re-tuned.
;
; SAFETY
;   * AVX2 only -- NO AVX-512, NO GFNI. Correct on the 5950X too, just tuned for
;     the wrong core there.
;   * Writes exactly the same bytes as the original; only the instruction width
;     used to write the interior differs.
;
; Void wia_setbits(RTL_BITMAP* bm, ulong StartingIndex, ulong NumberToSet)
;   [Win64: rcx, edx, r8d]
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
        cmp       r11, 1024                       ; 2ND PC: rep stosd from 4 KB (was 16 KB)
        jae       sb_rep
        cmp       r11, 32
        jae       sb_avx                          ; 2ND PC: 128 B+ -- Zen 4 has a native 256-bit store path
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
        ; 2ND PC note: rep STOSQ was tried here (same bytes, half the iterations)
        ; and measured WORSE on this core -- 0.86x at the 40000 class against
        ; 0.97x for STOSD. Kept as STOSD.
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
