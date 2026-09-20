# 020 — `RtlUpcaseUnicodeStringToAnsiString` (AVX2) — **LANDED** (core ntdll, 9.0× geomean)

Upcases *and* narrows UTF-16 → ANSI in one pass (used to normalize names for case-insensitive ANSI
comparison). Combines the 015 upcase and 018 narrow.

- **Contract (no-allocate):** `NTSTATUS RtlUpcaseUnicodeStringToAnsiString(ANSI_STRING* dst,
  const UNICODE_STRING* src, BOOLEAN Allocate)`, `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlUpcaseUnicodeStringToAnsiString`. **ISA:** AVX2.
- All-ASCII 16-wchar blocks upcase a–z in-register then pack 16 → 16 bytes; non-ASCII via a 65536-entry
  `wia_upansimap[]` (upcase∘codepage) table from the OS.

## Correctness — PASS

Output bytes + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`,
ASCII + non-ASCII.

## Correction found by live substitution (2026-09-20) — two rules, five changes

This export **NUL-terminates**, and nothing in this change knew that. The consequences were two,
and they hid each other:

1. **The size rule is one element larger than the conversion.** A 4-character source needs `MaximumLength >= 5`, not 4.
2. **A terminator is written at `[Length]` on success.**

`impl.asm` had neither, and `reference.c` had neither either — so the implementation and its own
oracle agreed perfectly and both disagreed with Windows.

On overflow this one writes **nothing** and leaves `dst->Length` as the caller had it. Its sibling
[018](../018-rtlunicodestringtoansistring/) does the opposite — it truncates and partially writes.
Four functions in one family, two failure disciplines, each measured rather than inferred from the
others.


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

| length | ours ns | ntdll ns | ratio |
|---:|---:|---:|---:|
| 8 | 4.45 | 14.09 | 3.16× |
| 128 | 7.81 | 90.92 | 11.64× |
| 4096 | 194.19 | 2550.00 | 13.13× |
| 32000 | 1612 | 20070 | 12.45× |

**Overall geomean 9.0× faster. No size class regressed → LANDS.**

## Reproduce
```
changes\020-rtlupcaseunicodestringtoansistring\build.bat
```
