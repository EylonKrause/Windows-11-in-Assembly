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

## Implementation — hybrid slicing-by-8 + VPCLMULQDQ fold
- **`Len < 128`:** lean slicing-by-8 (`rorx` byte-extraction, two-accumulator reduce) + byte tail. Beats
  ntdll at small sizes, where its slicing carries heavy per-call overhead (~0.9 GB/s at 32 B).
- **`Len >= 128`:** VPCLMULQDQ carry-less fold. The reflected-fold constants were **derived from the
  scalar recurrence** (not guessed): the reflected primitive is `icrc(k bytes, c) = mulx^{8k}(c ^ B)`, so a
  freshly-loaded 16-byte block reduces as `mulx^128(X.lo) ^ mulx^64(X.hi)`, and the fold that advances the
  accumulator by 128 bits is `X' = clmul(X.lo, KA) ^ clmul(X.hi, KB) ^ next16` with
  `KA = x^128 = 0xEADC41FD2BA3D420` and `KB = x^64 = 0x21E9761E252621AC` (found by a search that requires
  `reduce128(clmul(v,KA)) == mulx^256(v)` and `== mulx^192(v)`). The final 128→64 reduction is
  `crc = SLICE8(SLICE8(X.lo)) ^ SLICE8(X.hi)`, reusing the slicing tables because
  `SLICE8(v) == mulx^64(v) == icrc(v as 8 bytes, 0)` — one slicing-by-8 step. The whole PCLMUL path was
  validated in C to **0 mismatches vs both the scalar model and the live export** before porting to asm.

ISA: PCLMULQDQ + AVX + BMI2.

## Benchmark — vs live `ntdll!RtlCrc64` (`/Od`)
```
size      ours ns   system ns   ratio   ours GB/s   verdict
8           3.79       7.61      2.01x     2.11      BETTER   (slicing)
32         12.34      38.53      3.12x     2.59      BETTER   (slicing)
64         24.62      42.50      1.73x     2.60      BETTER   (slicing)
256        31.21      92.47      2.96x     8.20      BETTER   (pclmul)
1024      104.36     249.96      2.40x     9.81      BETTER   (pclmul)
4096      398.12     882.84      2.22x    10.29      BETTER   (pclmul)
65536    5931.25   13578.12      2.29x    11.05      BETTER   (pclmul)
geomean                          2.33x  => LANDS (no size class regressed)
```
The PCLMUL fold turns the earlier large-size losses (0.48–0.88×, slicing-only) into 2.1–3.0× wins:
~10–11 GB/s vs ntdll's ~4.8 GB/s slicing-by-8. A fold-by-4 (four independent accumulators to hide the
`pclmulqdq` latency) would push large sizes to ~20 GB/s; single-accumulator already lands everywhere.

## Reproduce
```
changes\076-rtlcrc64\build.bat
```
