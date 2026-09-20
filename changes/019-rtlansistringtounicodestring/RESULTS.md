# 019 — `RtlAnsiStringToUnicodeString` (AVX2) — **LANDED** (core ntdll, 11.58× geomean)

Widens a single-byte ANSI string to UTF-16 — the reverse of 018, and just as hot (every ANSI Win32 API
input path). ntdll does it one char at a time (~2 GB/s).

- **Contract (no-allocate):** `NTSTATUS RtlAnsiStringToUnicodeString(UNICODE_STRING* dst,
  const ANSI_STRING* src, BOOLEAN Allocate)` with `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlAnsiStringToUnicodeString`. **ISA:** AVX2.
- All-ASCII 16-byte blocks widen with `vpmovzxbw` (16 → 16 words); blocks with any byte `>= 0x80` use a
  256-entry `wia_a2umap[]` byte→wchar table built from the OS codepage.

## Correctness — PASS

Output words + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`,
ASCII + high-byte content, plus buffer-overflow.

## Correction found by live substitution (2026-09-20) — two rules, five changes

This export **NUL-terminates**, and nothing in this change knew that. The consequences were two,
and they hid each other:

1. **The size rule is one element larger than the conversion.** A 4-character source needs `MaximumLength >= 10`, not 8 — and the rule is on the BYTE count, so an odd `MaximumLength` of 11 succeeds where 9 fails.
2. **A terminator is written at `[Length]` on success.**

`impl.asm` had neither, and `reference.c` had neither either — so the implementation and its own
oracle agreed perfectly and both disagreed with Windows.

On overflow this one writes **nothing** and leaves `dst->Length` as the caller had it. Its sibling
[018](../018-rtlunicodestringtoansistring/) does the opposite — it truncates and partially writes.
Four functions in one family, two failure disciplines, each measured rather than inferred from the
others.


### Why the gate could not see it

The old `correctness.c` was blind to both rules *by construction*, which is the part worth keeping:

* it ran **every** case with `MaximumLength` fixed and generous (600), so the size rule never
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

| length (bytes) | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 8 | 6.24 | 12.03 | 1.93× | BETTER |
| 128 | 6.91 | 105.58 | 15.27× | BETTER |
| 512 | 18.29 | 405.05 | 22.15× | BETTER |
| 4096 | 140.05 | 3205.47 | **22.89×** | BETTER |
| 32000 | 1126 | 25077 | 22.27× | BETTER |

**Overall geomean 11.58× faster. No size class regressed → LANDS.**

## Reproduce
```
changes\019-rtlansistringtounicodestring\build.bat
```
