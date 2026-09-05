# 076 — `RtlCrc64` — **LANDS** (reverse-engineered + VPCLMULQDQ)

`ULONG64 RtlCrc64(const void* Source, SIZE_T Length, ULONG64 InitialCrc)` (ntdll). Previously on the
project's **deferred / "needs dedicated reverse-engineering"** list — it is a *non-standard* CRC
(`crc({00}, 0) != 0`). Now fully cracked **and** beaten at every size.

## The algorithm (reverse-engineered from the disassembly + confirmed empirically)
The exported `RtlCrc64` is a thunk `lea r9,[struct]; jmp worker`. The worker is a **standard reflected
byte-table CRC-64** — `crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF]` — with:

- **polynomial** `0x9A6C9329AC4BC9B5` (reflected form; it is literally `table[0x80]`, and is also cached at
  `struct[+0x18]` as a fold constant),
- `struct[+0x20] = 0xFFFFFFFFFFFFFFFF` used as **both** the init-xor and the xor-out, i.e. the internal
  accumulator starts at `~InitialCrc` and the result is `~crc`.

So `RtlCrc64(d, n, init) = ~( reflected_crc_core(d, ~init) )`. That `~/~` wrapping is why `crc({00},0)≠0`
even though `table[0]=0`. Our generated table matches ntdll's 256 entries exactly, and the model matches
the live export on **400,000 + 200,000 + 300,000** random inputs (varying length and `InitialCrc`,
including 0 and `~0`), 0 mismatches, plus a page-guard over-read test.

## Implementation — hybrid slicing-by-8 + 256-bit VPCLMULQDQ fold
- **`Len < 128`:** lean slicing-by-8 (`rorx` byte-extraction, two-accumulator reduce) + byte tail. Beats
  ntdll at small sizes, where its slicing carries heavy per-call overhead (~0.9 GB/s at 32 B).
- **`Len >= 128`:** VPCLMULQDQ carry-less fold. The reflected-fold constants were **derived from the
  scalar recurrence** (not guessed): the reflected primitive is `icrc(k bytes, c) = mulx^{8k}(c ^ B)`, so a
  freshly-loaded 16-byte block reduces as `mulx^128(X.lo) ^ mulx^64(X.hi)`, and advancing a 128-bit lane by
  `D` bits is `clmul(lane.lo, x^D) ^ clmul(lane.hi, x^(D-64))` (verified by a search requiring
  `reduce128(clmul(v, x^D)) == mulx^(D+128)(v)`). We fold **two 16-byte blocks per iteration in a 256-bit
  ymm accumulator** — each lane advanced 256 bits, `K256 = {x^256, x^192}` — because Zen3's ymm
  `vpclmulqdq` sustains ~1.7× the xmm clmul throughput (measured 3.85 vs 2.24 Gclmul/s). Then the two lanes
  are combined (advance the low lane 128 bits, xor the high lane, `K128 = {x^128, x^64}`) and any last
  16-byte block folded. The final 128→64 reduction is `crc = SLICE8(SLICE8(X.lo)) ^ SLICE8(X.hi)`, reusing
  the slicing tables because `SLICE8(v) == mulx^64(v) == icrc(v as 8 bytes, 0)`. The whole path was
  validated in C to **0 mismatches vs both the scalar model and the live export** before porting to asm.

ISA: VPCLMULQDQ + AVX2 + BMI2.

## Benchmark — vs live `ntdll!RtlCrc64` (`/Od`)
```
size      ours ns   system ns   ratio   ours GB/s   verdict
8           3.76       7.62      2.03x     2.13      BETTER   (slicing)
32         12.19      38.43      3.15x     2.63      BETTER   (slicing)
64         23.73      42.43      1.79x     2.70      BETTER   (slicing)
256        21.52      92.67      4.31x    11.89      BETTER   (ymm fold)
1024       57.31     249.70      4.36x    17.87      BETTER   (ymm fold)
4096      205.09     928.04      4.52x    19.97      BETTER   (ymm fold)
65536    3139.06   13585.94      4.33x    20.88      BETTER   (ymm fold)
geomean                          3.29x  => LANDS (no size class regressed)
```
The 256-bit fold turns the earlier slicing-only large-size losses (0.48–0.88×) into **4.3–4.5× wins** —
~20 GB/s vs ntdll's ~4.8 GB/s slicing-by-8. (A single-accumulator xmm fold reached ~11 GB/s / 2.3×; the
ymm fold nearly doubles it. An xmm fold-by-4 gave no gain — the bottleneck is `pclmulqdq` *throughput*, not
latency, so widening the clmul via ymm is what helps.)

## Reproduce
```
changes\076-rtlcrc64\build.bat
```
