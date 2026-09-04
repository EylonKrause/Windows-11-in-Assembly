# 009 — `RtlHashUnicodeString` (AVX2) — **LANDED** (core ntdll, 4.5× geomean)

Computes the X65599 name hash used across the OS for hash-based name lookups (atom tables, and other
`UNICODE_STRING` hash keys). One of the slowest ntdll primitives measured (~2.2 GB/s).

- **Contract:** `NTSTATUS RtlHashUnicodeString(const UNICODE_STRING*, BOOLEAN CaseInSensitive,
  ULONG Algorithm, PULONG HashValue)` — default algorithm 0 = `X65599`:
  `h = 0; for each char c (upcased if CI): h = h*65599 + c` (mod 2^32).
- **Compared against:** live `ntdll.dll!RtlHashUnicodeString` on this PC.
- **ISA:** AVX2.

## How it works

The `h = h*65599 + c` recurrence is a base-65599 polynomial and is sequential — each step waits on the
previous multiply. It is parallelized 8 characters at a time:

```
h = h*P^8 + (c0*P^7 + c1*P^6 + ... + c7*P^0)
```

The weighted inner sum is one `vpmulld` of 8 chars against a precomputed `[P^7..P^0]` vector followed by a
horizontal add; only the `h*P^8` step stays on the critical path, so it runs once per 8 chars instead of 8
dependent multiplies. Case-insensitive upcases each block **in-register** (a–z range subtract) for the
all-ASCII common case, falling back to a `wia_upcase[]` lookup only for blocks containing a wchar `>= 0x80`.

## Correctness — PASS

Bit-exact vs a scalar reference **and** live `ntdll!RtlHashUnicodeString` across length `0..300` × both
modes, ASCII and non-ASCII. The scalar and the 8-lane models were both validated against ntdll (0
mismatches / 80,000) before porting.

## Speed — LANDS (no size class regressed)

Min-of-200, pinned core, equal strings.

| length | mode | ours ns | ntdll ns | ratio | verdict |
|---:|:--|---:|---:|---:|:--|
| 8 | cs | 2.90 | 4.45 | 1.54× | BETTER |
| 128 | cs | 13.07 | 103.1 | 7.89× | BETTER |
| 512 | cs | 54.57 | 445.3 | 8.16× | BETTER |
| 32000 | cs | 3682 | 28513 | 7.74× | BETTER |
| 8 | CI | 4.24 | 5.79 | 1.37× | BETTER |
| 128 | CI | 22.58 | 110.0 | 4.87× | BETTER |
| 512 | CI | 87.25 | 452.2 | 5.18× | BETTER |
| 32000 | CI | 5392 | 28523 | 5.29× | BETTER |

**Overall geomean 4.50× faster. No size class regressed → LANDS.**

## Iteration (the "don't give up" fix)

A first CI version upcased each char through the table into a 16-byte scratch buffer, then reloaded it with
`vpmovzxwd`. The 8 small stores feeding one wide load caused a store-forwarding stall — CI ran **0.54–0.75×
(WORSE)**. Replacing it with an in-register ASCII upcase (no store/reload; table only for non-ASCII blocks)
made CI **1.37–5.29×**, and the change LANDED.

## Reproduce
```
changes\009-rtlhashunicodestring\build.bat
```
