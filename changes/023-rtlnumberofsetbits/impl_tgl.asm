; changes/023-rtlnumberofsetbits/impl_tgl.asm
; ULONG wia_numsetbits(const RTL_BITMAP* bm)   [Win64: rcx -> eax]
;
; Tiger Lake / Willow Cove variant of change 023. Same contract, same oracle, same gates; the only
; file that differs from the parent is this one.
;
; Why a variant and not an edit
; -----------------------------
; The parent counts one 64-bit word per POPCNT and accumulates into a single register. On Zen 3 that
; ties ntdll at 1 Mb (1.00x) and wins small, which is what its RESULTS.md records. On this machine
; the same code measures 0.74x at 1 Mb, and the reason is in the loop shape rather than in the data:
;
;   * POPCNT is one per cycle, so 64 bits per cycle is the hard ceiling of that loop, and
;   * every iteration adds into the SAME accumulator, so the adds form one serial dependency chain.
;
; This part has AVX512VPOPCNTDQ. `vpopcntq zmm` population-counts eight qwords -- 512 bits -- in a
; single instruction, and two independent accumulators break the chain. ntdll cannot do this: it
; ships one binary for every x86-64 Windows machine, including the large majority with no AVX-512 at
; all, so its large-bitmap path is stuck at what POPCNT can do. That asymmetry is the entire win
; here, and it exists only on a machine like this one.
;
; The parent is left untouched: its measurement was taken on hardware that has no AVX-512, and
; editing the implementation its RESULTS.md describes would re-attribute that measurement to a
; machine that never ran it.
;
; SHAPE
;   >= 1024 bits : two vpopcntq per iteration, 128 B/iter, two accumulators, horizontal sum once
;      512..1023 : one vpopcntq
;         < 512  : the parent's scalar ladder, byte-for-byte -- see below
;
; The small sizes deliberately execute the PARENT'S code path. They are where change 023 already
; wins (2.34x at 64 bits) and where a vector setup could only cost; a variant that regressed the
; small classes to win the large one would fail the same gate the parent passed.
;
; PAGE SAFETY: unchanged from the parent. Every load is inside the bits the function has been told
; to read -- the 1024-bit step consumes exactly the 128 bytes it loads -- so this reads no byte the
; parent would not also have read.
;
; RTL_BITMAP = { ULONG SizeOfBitMap @0; PULONG Buffer @8 }.
; ISA: AVX512F + AVX512VPOPCNTDQ, plus POPCNT for the tail. Validated on bench #3
; (Intel i9-11900H, Tiger Lake-H) -- see docs/PLATFORM-i9-11900H.md.
;
; ABI: uses zmm0-zmm3 only. xmm0-xmm5 are volatile under Win64, so nothing here needs a spill;
; tools/abi-audit.py and tools/abi-check both cover this.

.code
wia_numsetbits PROC
        mov       r9d, dword ptr [rcx]             ; SizeOfBitMap (bits)
        mov       r10, [rcx + 8]                   ; Buffer
        xor       eax, eax                         ; running count
        xor       r11d, r11d                       ; byte offset

        cmp       r9d, 1024                        ; enough to pay for the vector setup?
        jb        tail64

        vpxorq    zmm0, zmm0, zmm0                 ; accumulator A
        vpxorq    zmm1, zmm1, zmm1                 ; accumulator B

        ; 1024 bits per iteration. The two vpopcntq are independent, and they feed two separate
        ; accumulators, so neither the counts nor the adds serialise against each other.
lp1024:
        vpopcntq  zmm2, zmmword ptr [r10 + r11]
        vpopcntq  zmm3, zmmword ptr [r10 + r11 + 64]
        vpaddq    zmm0, zmm0, zmm2
        vpaddq    zmm1, zmm1, zmm3
        add       r11, 128
        sub       r9d, 1024
        cmp       r9d, 1024
        jae       lp1024

        ; one more 512-bit step if the remainder still covers a full zmm -- otherwise this would be
        ; up to eight scalar POPCNTs, which is the thing being avoided.
        cmp       r9d, 512
        jb        hsum
        vpopcntq  zmm2, zmmword ptr [r10 + r11]
        vpaddq    zmm0, zmm0, zmm2
        add       r11, 64
        sub       r9d, 512

hsum:
        ; 8 qwords -> 1. Counts cannot overflow a qword lane: the largest bitmap ULONG can describe
        ; is 2^32-1 bits, so a lane holds at most 2^32/64 * 64 well inside 64 bits.
        vpaddq    zmm0, zmm0, zmm1
        vextracti64x4 ymm1, zmm0, 1
        vpaddq    ymm0, ymm0, ymm1
        vextracti128  xmm1, ymm0, 1
        vpaddq    xmm0, xmm0, xmm1
        vpunpckhqdq xmm1, xmm0, xmm0
        vpaddq    xmm0, xmm0, xmm1
        vmovq     rdx, xmm0
        add       rax, rdx
        vzeroupper

        ; ---- from here down this is the parent's scalar ladder, unchanged -------------------
tail64:
        cmp       r9d, 64
        jb        rem
        popcnt    r8, qword ptr [r10 + r11]
        add       rax, r8
        add       r11, 8
        sub       r9d, 64
        jmp       tail64
rem:
        test      r9d, r9d
        jz        done
        cmp       r9d, 32
        jb        last
        mov       r8d, dword ptr [r10 + r11]
        popcnt    r8d, r8d
        add       eax, r8d
        add       r11, 4
        sub       r9d, 32
last:
        test      r9d, r9d
        jz        done
        mov       r8d, dword ptr [r10 + r11]
        mov       ecx, r9d
        mov       edx, 1
        shl       edx, cl
        dec       edx                              ; mask = (1 << bits) - 1
        and       r8d, edx
        popcnt    r8d, r8d
        add       eax, r8d
done:
        ret
wia_numsetbits ENDP
END
