# 004 — `wcscmp` (AVX2) — **LANDED**

Lexicographic compare of two UTF-16 strings. Hot in sorting, registry key comparison, and any ordered
container of wide strings.

- **Contract:** `int wcscmp(const wchar_t* a, const wchar_t* b)` — sign of the first differing wchar, `0`
  if equal. (The standard makes only the **sign** significant; this is what the test checks.)
- **Compared against:** live `ucrtbase.dll!wcscmp` on this PC.
- **ISA:** AVX2 + BMI1 (`tzcnt`). Two unbounded pointers, so neither can be aligned.

## The page-safety problem (why this one is harder)

You cannot align both pointers at once, and a naive 32-byte load can read past a terminator into an
unmapped page. Solution: a 32-byte vector compare is issued **only when both pointers have ≥ 32 bytes to
their page end** (`offset & 4095 ≤ 4064`), which guarantees both loads sit inside already-mapped pages.
Within 32 bytes of either page boundary it drops to a one-wchar-at-a-time scalar step, which is inherently
safe because it stops at the terminator and never reads past it. Boundaries occur once per 4 KB, so the
scalar path costs almost nothing.

## Correctness — PASS

vs scalar reference and live `ucrtbase` `wcscmp`, comparing **sign**:
- Fuzz: length `0..280` × offset `0..7`; equal strings, `a>b` and `a<b` injected at many positions, and
  the prefix case (one string a proper prefix of the other).
- **Two-string page-guard:** both terminators placed right before separate `PAGE_NOACCESS` pages; equal
  and last-wchar-differ cases. Zero faults, zero sign mismatches.

## Speed — LANDS (no size class regressed)

Min-of-200 batches, pinned core. **Equal** strings (worst case: scan to the terminator).

| length (wchars) | ours ns | ucrtbase ns | ratio | ours GB/s* | verdict |
|---:|---:|---:|---:|---:|:--|
| 3 | 2.45 | 3.12 | 1.27× | 2.5 | BETTER |
| 15 | 3.12 | 5.79 | 1.86× | 9.6 | BETTER |
| 63 | 4.46 | 17.15 | 3.84× | 28.3 | BETTER |
| 255 | 15.35 | 63.92 | 4.16× | 33.2 | BETTER |
| 1023 | 58.67 | 235.25 | 4.01× | 34.9 | BETTER |
| 8191 | 492.16 | 1839.51 | 3.74× | 33.3 | BETTER |

*GB/s counts one stream; `wcscmp` reads two, so memory traffic is ~2× this (~68 GB/s), which is why the
per-stream number is lower than the single-stream scans.

**Overall geomean 2.88× faster. No size class regressed → LANDS.** The shipped `ucrtbase` `wcscmp` is
effectively scalar; the AVX2 compare wins 3.7–4.2× on medium-and-larger strings.

## Reproduce
```
changes\004-wcscmp\build.bat
```
