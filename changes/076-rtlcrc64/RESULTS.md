# 076 — `RtlCrc64` — **reverse-engineered & validated** (perf PARKED at large sizes)

`ULONG64 RtlCrc64(const void* Source, SIZE_T Length, ULONG64 InitialCrc)` (ntdll). Previously on the
project's **deferred / "needs dedicated reverse-engineering"** list — it is a *non-standard* CRC
(`crc({00}, 0) != 0`, so not a plain reflected CRC at first glance). Now fully cracked.

## The algorithm (reverse-engineered from the disassembly + confirmed empirically)
The exported `RtlCrc64` is a thunk `lea r9,[struct]; jmp worker`. The worker is a **standard reflected
byte-table CRC-64** — `crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF]` — with:

- **polynomial** `0x9A6C9329AC4BC9B5` (reflected form; it is literally `table[0x80]`, and is also cached at
  `struct[+0x18]` as the fold constant),
- `struct[+0x20] = 0xFFFFFFFFFFFFFFFF` used as **both** the init-xor and the xor-out, i.e. the internal
  accumulator starts at `~InitialCrc` and the result is `~crc`.

So `RtlCrc64(d, n, init) = ~( reflected_crc_core(d, ~init) )`. That `~/~` wrapping is why `crc({00},0)≠0`
even though `table[0]=0` — the memory note's "non-standard" was the init/xorout wrapping, not the core.

Validated: our generated table matches ntdll's 256 entries exactly, and the full model matches the live
export on **200,000** + **400,000** random inputs (varying length and `InitialCrc`, including 0 and `~0`),
0 mismatches, plus a page-guard over-read test.

## Implementation & benchmark
Lean **slicing-by-8** (8 bytes/iter, 8-table lookup) with `rorx` byte-extraction and a two-accumulator
reduce, + byte-table tail. `wia_crc64_tab` built once by `wia_crc64_init`.

```
size      ours ns   system ns   ratio   ours GB/s   verdict
8           3.72       7.62      2.05x     2.15      BETTER
32         12.06      38.35      3.18x     2.65      BETTER
64         25.30      42.46      1.68x     2.53      BETTER
256       106.16      93.12      0.88x     2.41      WORSE
1024      403.64     249.79      0.62x     2.54      WORSE
4096     1614.13     882.34      0.55x     2.54      WORSE
65536   25932.81   13581.25      0.52x     2.53      WORSE
=> PARKED (large-size classes regress)
```

We **beat ntdll at small sizes** (8–64 B, where its slicing carries too much per-call overhead — 0.9 GB/s
at 32 B) but **lose at ≥256 B**: single-stream slicing-by-8 is loop-carried-dependency bound at ~2.5 GB/s,
while ntdll's slicing peaks ~4.8 GB/s. Neither `rorx` extraction nor the two-accumulator split crossed that
wall — the limit is the serial `crc → crc` recurrence, not the ALU work.

## Path to a full win (future work)
Matching/beating ntdll at large sizes needs to break the single-stream dependency: **VPCLMULQDQ fold-by-N**
(the poly's fold constants are derivable from `0x9A6C9329AC4BC9B5`; expect ~15–30 GB/s → 3–6× over ntdll's
slicing) or a **multi-stream interleave** with a GF(2) `x^n mod P` combine. Recorded honestly as PARKED for
now — the deferred *reverse-engineering* is the deliverable here and it is complete; the correct algorithm
is documented and bit-exact.

## Reproduce
```
changes\076-rtlcrc64\build.bat
```
