# 295 — `ntdll!RtlUnicodeStringToInteger` — **LANDS** (1.63× geomean, 5 clean runs of 5, EVERY ROW BETTER)

- **Contract:** `NTSTATUS RtlUnicodeStringToInteger(const UNICODE_STRING* s, ULONG Base, ULONG* Value)`
- **Compared against:** live `ntdll!RtlUnicodeStringToInteger` via `GetProcAddress`. `ntdll.dll`
  10.0.26100.9278, Windows 11 Pro 25H2 build 26200.9457.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **Fan-in:** **25** distinct live desktop/startup modules bind it.
- **Correctness:** **PASS — 3,643,732 checks.**
- **Gates:** ABI audit PASS; vector-re-entry audit clean; speed **LANDS in 5 runs of 5**, geomean
  1.591×–1.662×, and **no row in any run was below 1.00×**.

## Why this one

[`discovery/ntdll_tier3.c`](../../discovery/ntdll_tier3.c) measured the shipped export at **0.66 ns
per byte** on digit strings — roughly **thirty times worse per byte than anything else in that
table**. 11.95 ns to parse nine digits, 13.15 for ten, and **25.70 ns for a `0x` prefix in base 0**.

Its neighbours in the same sweep were ruled out on the same evidence: `RtlUpcaseUnicodeChar` is
1.55 ns flat despite 32 modules binding it, and `RtlMultiByteToUnicodeSize` takes the same 14.3 ns
for a 254-byte and a 4095-byte subject because it does not scan at all.

## Speed

| subject | ours ns | ntdll ns | ratio |
|---|---:|---:|---:|
| dec 1 digit | 3.44 | 4.95 | 1.44× |
| dec 9 digits | 6.81 | 12.28 | **1.80×** |
| dec 10 digits | 7.77 | 12.69 | 1.63× |
| dec 4294967295 | 7.99 | 13.17 | 1.65× |
| dec wrap 2^32 | 7.60 | 12.52 | 1.65× |
| dec 64 digits | 43.05 | 74.36 | 1.73× |
| dec 256 digits | 147.54 | 277.15 | **1.88×** |
| base 0 `0xDEADBEEF` | 9.27 | 19.67 | **2.12×** |
| base 16, 64 digits | 50.91 | 120.02 | **2.36×** |
| base 2, 16 bits | 11.49 | 29.12 | **2.54×** |
| base 2, 32 bits | 17.86 | 44.79 | 2.51× |
| base 8 `7777` | 5.73 | 9.47 | 1.65× |
| whitespace + negative | 8.67 | 14.73 | 1.70× |
| leading NUL treated as whitespace | 5.57 | 7.62 | 1.37× |
| plus sign | 4.01 | 6.06 | 1.51× |
| no digits at all | 3.72 | 5.15 | 1.38× |
| all whitespace | 6.50 | 7.04 | 1.08× |
| high char stops the parse | 3.62 | 4.87 | 1.34× |
| digit outside the base | 3.72 | 5.15 | 1.38× |
| `Length` = 0 (invalid) | 2.58 | 3.90 | 1.51× |
| **odd `Length`** (invalid) | 2.58 | 3.90 | 1.51× |
| invalid base 36 | 3.18 | 4.58 | 1.44× |

**The refusal paths are named rows, not omissions.** A parser that is fast only when its input is
valid has optimised the wrong half — roughly a third of this table is inputs the function rejects,
and every one of them is faster too.

## The contract, probed rather than read

Its ANSI sibling is change **129 `RtlCharToInteger`**, whose RESULTS.md records that *the shipped
export was itself wrong* — it did not step over a leading NUL. That is the reason every point below
was established against the running export instead of from documentation:

* **`Length` is in BYTES and an ODD `Length` is rejected** with `STATUS_INVALID_PARAMETER`
  (`0xC000000D`), as is `Length == 0`. The string is **counted, not NUL-terminated**.
* **Leading whitespace is every code unit `<= 0x20`**, which means an embedded **NUL is skipped as
  whitespace** rather than terminating the parse — the counterpart of 129's finding, and it has its
  own bench row.
* **Accepted explicit bases are 2, 8, 10 and 16 only.** Anything else — including 36, which `strtoul`
  accepts — is `STATUS_INVALID_PARAMETER`.
* **Base 0 infers from the prefix**: `0x`, `0o`, `0b`, each accepted in either case, otherwise
  decimal. The tier-3 timings showed `0x` costing 25.70 ns against 8.50 for `0o`, which was the hint
  that the three are not one code path in the shipped version.
* **`*Value` is written on failure** — set to 0 — rather than left untouched. The harness checks the
  word **past** `*Value` too, to prove nothing is written beyond it.
* A `+` or `-` sign is accepted; overflow **wraps** rather than saturating or failing, verified at
  the exact boundary (`4294967295` and `2^32`), which is why both are bench rows.

## Correctness — 3,643,732 checks

`NTSTATUS` **and** `*Value` **and** no write past it, over: the edge values × **26 bases**; every
`Length` from 0 to 160 **including odd ones**; **all 65,536 code units** × 5 positions × 5 bases;
unaligned buffers; a `PAGE_NOACCESS` guard at **every length 1..64**; and a 2M fuzz set.

The all-code-units sweep is what makes the whitespace and digit-classification rules trustworthy
rather than sampled — every possible UTF-16 code unit was tried in five positions.

## ISA and dispatch

AVX2 baseline; no CPUID dispatch is needed because nothing above it is used. The 256-entry digit
table and the branchless mul-carry overflow guard that needs no division come from changes
**111 `strtoul`** (2.2×) and **112 `_strtoi64`** (1.8×). The shape is a peeled first digit followed
by a rotated steady-state loop — load at the top, conditional back-edge — so the common case of a
short decimal number pays for one branch rather than a loop prologue.
