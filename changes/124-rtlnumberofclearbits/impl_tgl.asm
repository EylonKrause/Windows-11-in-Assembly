; changes/124-rtlnumberofclearbits/impl_tgl.asm
; ULONG wia_numclearbits(const RTL_BITMAP* bm)   [Win64: rcx -> eax]
;
; Tiger Lake / Willow Cove variant of change 124, and the exact counterpart of
; changes/023-rtlnumberofsetbits-tgl -- same cause, same fix, complementary answer.
;
; WHY A VARIANT AND NOT AN EDIT
; -----------------------------
; The parent computes clear = SizeOfBitMap - popcount(set), counting with a scalar POPCNT loop that
; accumulates into a single register. POPCNT retires one per cycle, so 64 bits per cycle is the hard
; ceiling, and every iteration adds into the SAME accumulator, which makes one serial dependency
; chain out of the whole loop. On Zen 3 that still beats ntdll everywhere; here the 1 Mb class
; measures 0.81x.
;
; This part has AVX512VPOPCNTDQ. `vpopcntq zmm` population-counts eight qwords -- 512 bits -- per
; instruction, and two accumulators break the chain. ntdll cannot use it: one binary ships to every
; x86-64 Windows machine and most of them have no AVX-512 at all, so its bulk path is stuck at what
; POPCNT can do. That asymmetry is the whole win, and it exists only on hardware like this.
;
; The subtraction is unchanged and stays where it was: the vector path computes the SET count, and
; the single `n - set` at the end converts it. Clear-counting directly (complement each word, then
; popcount) would be wrong at the final partial word, where the bits past SizeOfBitMap must not be
; counted -- complementing turns those don't-care zeros into ones. The parent already gets this
; right by masking the last word and counting SET bits; changing that would be changing the
; arithmetic, which is not what this variant is for.
;
; SHAPE
;   >= 1024 bits : two vpopcntq per iteration, 128 B/iter, two accumulators
;      512..1023 : one vpopcntq
;         < 512  : the parent's scalar ladder, byte-for-byte, including the masked final word
;
; The small classes deliberately run the PARENT'S code. They are where change 124 already wins and
; where a vector prologue could only cost; a variant that regressed them to win 1 Mb would fail the
; same gate the parent passed.
;
; PAGE SAFETY: unchanged. The 1024-bit step consumes exactly the 128 bytes it loads, so this reads
; no byte the parent would not also have read.
;
; RTL_BITMAP = { ULONG SizeOfBitMap @0; PULONG Buffer @8 }.
; ISA: AVX512F + AVX512VPOPCNTDQ, plus POPCNT for the tail. Validated on bench #3
; (Intel i9-11900H, Tiger Lake-H) -- see docs/PLATFORM-i9-11900H.md.
;
; ABI: zmm0-zmm3 only; xmm0-xmm5 are volatile under Win64, so nothing needs a spill.

.code
wia_numclearbits PROC
        mov       r9d, dword ptr [rcx]             ; n (working counter, bits)
        mov       r10, [rcx + 8]                   ; Buffer
        mov       r11d, r9d                        ; save original n for the final subtract
        xor       eax, eax                         ; SET-bit count
        xor       edx, edx                         ; byte offset

        cmp       r9d, 1024                        ; enough to pay for the vector setup?
        jb        tail64

        vpxorq    zmm0, zmm0, zmm0                 ; accumulator A
        vpxorq    zmm1, zmm1, zmm1                 ; accumulator B
lp1024:
        vpopcntq  zmm2, zmmword ptr [r10 + rdx]
        vpopcntq  zmm3, zmmword ptr [r10 + rdx + 64]
        vpaddq    zmm0, zmm0, zmm2
        vpaddq    zmm1, zmm1, zmm3
        add       rdx, 128
        sub       r9d, 1024
        cmp       r9d, 1024
        jae       lp1024

        cmp       r9d, 512                         ; one more full zmm beats eight scalar POPCNTs
        jb        hsum
        vpopcntq  zmm2, zmmword ptr [r10 + rdx]
        vpaddq    zmm0, zmm0, zmm2
        add       rdx, 64
        sub       r9d, 512

hsum:
        ; 8 qwords -> 1. No lane can overflow: the largest bitmap a ULONG can describe is 2^32-1
        ; bits, so a lane holds at most 2^32/64 * 64 counts, well inside 64 bits.
        vpaddq    zmm0, zmm0, zmm1
        vextracti64x4 ymm1, zmm0, 1
        vpaddq    ymm0, ymm0, ymm1
        vextracti128  xmm1, ymm0, 1
        vpaddq    xmm0, xmm0, xmm1
        vpunpckhqdq xmm1, xmm0, xmm0
        vpaddq    xmm0, xmm0, xmm1
        vmovq     rcx, xmm0
        add       rax, rcx
        vzeroupper

        ; ---- from here down this is the parent's scalar ladder, unchanged ------------------
tail64:
        cmp       r9d, 64
        jb        rem
        popcnt    r8, qword ptr [r10 + rdx]
        add       rax, r8
        add       rdx, 8
        sub       r9d, 64
        jmp       tail64
rem:
        test      r9d, r9d
        jz        done
        cmp       r9d, 32
        jb        last
        mov       r8d, dword ptr [r10 + rdx]
        popcnt    r8d, r8d
        add       eax, r8d
        add       rdx, 4
        sub       r9d, 32
last:
        test      r9d, r9d
        jz        done
        mov       r8d, dword ptr [r10 + rdx]
        mov       ecx, r9d
        mov       edx, 1
        shl       edx, cl
        dec       edx                              ; mask = (1 << bits) - 1
        and       r8d, edx                         ; bits past SizeOfBitMap must not be counted
        popcnt    r8d, r8d
        add       eax, r8d
done:
        mov       ecx, r11d                        ; original n
        sub       ecx, eax                         ; clear = n - set
        mov       eax, ecx
        ret
wia_numclearbits ENDP
END
