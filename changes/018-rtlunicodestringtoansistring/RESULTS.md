# 018 — `RtlUnicodeStringToAnsiString` (AVX2) — **LANDED** (core ntdll, 12.79× geomean)

Converts a UTF-16 string to single-byte ANSI — one of the most-called interop conversions (every ANSI
Win32/RPC path). ntdll does it one char at a time (~2.1 GB/s); this is the biggest win in the repo.

- **Contract (no-allocate):** `NTSTATUS RtlUnicodeStringToAnsiString(ANSI_STRING* dst,
  const UNICODE_STRING* src, BOOLEAN Allocate)` with `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlUnicodeStringToAnsiString`. **ISA:** AVX2.
- All-ASCII 16-wchar blocks pack 16 → 16 bytes (`vpackuswb` + `vpermq`); blocks with any wchar `>= 0x80` use
  `wia_ansimap[]` (a 65536-entry wchar→byte table built from the OS codepage), so the mapping is bit-exact.

## Correctness — PASS

Output bytes + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`,
ASCII + non-ASCII, plus buffer-overflow.

## Correction found by live substitution (2026-09-20) — two rules, five changes

This export **NUL-terminates**, and nothing in this change knew that. The consequences were two,
and they hid each other:

1. **The size rule is one element larger than the conversion.** A 4-character source needs `MaximumLength >= 5`, not 4.
2. **A terminator is written at `[Length]` on success.**

`impl.asm` had neither, and `reference.c` had neither either — so the implementation and its own
oracle agreed perfectly and both disagreed with Windows.

**And this one truncates rather than refusing**, which none of its four siblings do:

```
   Max = 0     -> STATUS_BUFFER_OVERFLOW, nothing written, Length left alone
   Max >= 1    -> n = min(srclen, Max-1) characters converted, NUL at [n], Length = n,
                  STATUS_SUCCESS if n == srclen else STATUS_BUFFER_OVERFLOW
```

so an 8-character source with `MaximumLength` 4 comes back as `"ABC\0"` with `Length` 3 and an
overflow status — a **partial write on failure**. Changes 019, 020, 024 and 025 all refuse outright
and leave the destination untouched. Four functions in one family, two failure disciplines, and the
only way to know which is which is to measure each one.

The truncation status has to survive a conversion loop that uses every volatile register, so it is
parked in the **caller's shadow space** — legitimate here because this is a leaf that allocates no
frame, and `[rsp+8]` is the home slot the caller already reserved for `rcx`.


### Why the gate could not see it

The old `correctness.c` was blind to both rules *by construction*, which is the part worth keeping:

* it ran **every** case with `MaximumLength` fixed and generous (300), so the size rule never
  bound and the overflow path was never compared against ntdll at all. The single overflow
  assertion it did have checked only that **our** function returned `0x80000005` — no live call, no
  buffer comparison;
* it compared indices `0..Length-1`, so the byte at `[Length]` — the only place a terminator can be
  — was outside the comparison by construction.

Two independent blind spots, each of which alone would have hidden one rule. A gate can be run
thousands of times and still never ask the question.

`correctness.c` now sweeps `MaximumLength` across the whole neighbourhood of the boundary for every
length (and 0..80 one at a time against a fixed 32-element source, on both the in-register and the
table path), and compares the **whole destination buffer** byte for byte against the live export
*and* the oracle, poison included.

## Live substitution
[`live-substitution/build_ntconv2_live.bat`](../../live-substitution/): **PASS** after the fix —
10000 cases, 0 differ, then reverted and re-verified.

## Speed — LANDS (no size class regressed)

| length (wchars) | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 8 | 4.68 | 13.59 | 2.90× | BETTER |
| 128 | 7.13 | 127.17 | 17.82× | BETTER |
| 512 | 23.19 | 490.67 | 21.15× | BETTER |
| 4096 | 178.64 | 3889.84 | 21.77× | BETTER |
| 32000 | 1346 | 30375 | **22.57×** | BETTER |

**Overall geomean 12.79× faster. No size class regressed → LANDS.**

## Reproduce
```
changes\018-rtlunicodestringtoansistring\build.bat
```
