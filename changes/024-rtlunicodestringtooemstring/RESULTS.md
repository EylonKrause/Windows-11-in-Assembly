# 024 — `RtlUnicodeStringToOemString` (AVX2) — **LANDED** (core ntdll, 7.68× geomean)

UTF-16 → OEM codepage narrowing (console output, 8.3 short filenames). Same machinery as 018 with the OEM
codepage table instead of ANSI.

- **Contract (no-allocate):** `NTSTATUS RtlUnicodeStringToOemString(OEM_STRING* dst,
  const UNICODE_STRING* src, BOOLEAN Allocate)`, `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlUnicodeStringToOemString`. **ISA:** AVX2.
- All-ASCII 16-wchar blocks pack 16 → 16 bytes; non-ASCII via `wia_oemmap[]` (65536-entry wchar→byte OEM
  table from the OS).

## Correctness — PASS

Output + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + non-ASCII) and overflow.

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

## Speed — LANDS

geomean **7.68×**; 8 → 3.28×, 512 → 10.38×, 32000 → 10.60×. No size class regressed.

## Reproduce
```
changes\024-rtlunicodestringtooemstring\build.bat
```
