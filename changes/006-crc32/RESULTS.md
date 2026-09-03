# 006 — `crc32` via VPCLMULQDQ — **PARKED** (ntdll's CRC is already PCLMUL-optimal)

Reimplements `ntdll!RtlComputeCrc32` (standard CRC-32/ISO-HDLC, poly 0xEDB88320, check value
`0xCBF43926`) using carry-less multiply folding. Correct, but does **not** beat the shipped function —
because the shipped function is already hardware-accelerated.

- **Compared against:** live `ntdll.dll!RtlComputeCrc32` on this PC.
- **ISA:** SSE2 + PCLMUL (VEX). VPCLMULQDQ is present on this Zen3 bench.

## Convention (nailed first)

`RtlComputeCrc32` is **standard CRC-32/ISO-HDLC** — confirmed by matching `~crcA(~init)` over 20,000
random cases and the canonical check value `RtlComputeCrc32(0,"123456789",9) == 0xCBF43926`. So the known
reflected-CRC PCLMUL fold constants apply directly.

## Correctness — PASS

`impl.asm` (committed) is a PCLMUL fold-by-1 that finishes the folded 16 bytes with the byte table (a
structure validated in C against the ntdll oracle before porting — 0 mismatches). Bit-exact vs the scalar
reference **and** live `ntdll!RtlComputeCrc32` across the check value, fuzz `n=0..1200`, unaligned starts,
and 4 KB–64 KB blocks. `check = 0xCBF43926`.

## Speed — PARKED

Two implementations measured against live ntdll:

| variant | 64 KB | 1 MB | verdict |
|---|---|---|---|
| committed fold-by-1 | 0.58× (10 GB/s vs 18) | 0.58× | latency-bound, single accumulator |
| fold-by-4 C model | **1.00× (17.7 vs 17.8 GB/s)** | **1.00× (17.8 vs 17.9)** | ties exactly |

The fold-by-1 loses because each iteration's `pclmul → pclmul → xor` dependency chain is serial and
PCLMUL latency on Zen3 is ~4–5 cycles. Fold-by-4 (four independent accumulators over 64 B/iter) hides that
and reaches **17.8 GB/s — identical to ntdll**. Both plateau at the same rate, and small inputs (64–256 B)
still favor ntdll's leaner setup.

## Conclusion

**`ntdll!RtlComputeCrc32` is already VPCLMULQDQ-accelerated** (~18 GB/s), not the naive byte table the
"beat it with carry-less multiply" premise assumed. We match it at large sizes and lose at small — so it
does not land, same as `memcmp`. Correct code kept as the record; the CRC family on this OS build is
already optimal. (A fold-by-8 variant might squeeze a few percent, but ntdll would likely track it — not
worth the risk for a non-decisive win.)

## Reproduce
```
changes\006-crc32\build.bat
```
