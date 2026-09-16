# 027 — `RtlUpcaseUnicodeToMultiByteN` (AVX2) — **LANDED** (core ntdll, 4.23× geomean over every input class)

The raw counted upcase-and-narrow converter (UTF-16 → ANSI, upcasing along the way). Combines the 015
upcase and 021 narrow with the counted N-interface.

- **Contract:** `NTSTATUS RtlUpcaseUnicodeToMultiByteN(char* dst, ULONG maxBytes, PULONG outLen,
  const wchar_t* src, ULONG srcBytes)`. Reports `STATUS_BUFFER_OVERFLOW` on truncation (unlike
  `RtlUnicodeToMultiByteN`, which returns success).
- **Compared against:** live `ntdll.dll!RtlUpcaseUnicodeToMultiByteN`. **ISA:** AVX2.
- All-ASCII 16/8-wide blocks upcase a–z in-register then pack; non-ASCII via `wia_upansimap[]`.

## Correctness — PASS

Output + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + non-ASCII) and truncating `maxBytes`.

## The table used to be `L'a' + (k & 15)` and nothing else — 2026-09-16

For an upcase conversion with a **table path**, that is the wrong table to publish. ASCII is the one
case the vector block handles; every character above `0x7F` goes through the 65536-entry map, and
that path had never been timed. `discovery/upcase_nonascii_rows.c` (commit `2e3ca3b`) asked, and got
**0.59× on Cyrillic or CJK** — geomean 1.900× over 24 rows, which is PARKED.

**The cause was not the table.** The scalar walk converted ONE character and jumped back to the top
of the loop, which re-ran the 16-wide test *and* the 8-wide test — two vector loads, two `VPTEST`s
and four bound comparisons — to discover once more that the character in front of it is not ASCII.
On text where the vector block never applies, that was paid on every character for the whole string.
It is [change 263's rule](../263-rtlcompareunicodestrings/RESULTS.md): *a scalar walk must not
re-enter a vector loop.*

**And the sibling already had it right.** Change 020, `RtlUpcaseUnicodeStringToAnsiString`, does the
same job with the same table and converts **sixteen** characters per visit — 2.62× on the same text
where this was 0.59×. That is what makes this a slip rather than a design question, and the fix is
its `a_sblock`, bounded here by *both* limits this function has: characters remaining and output room
remaining.

## Speed — LANDS (no size class regressed)

Min-of-100, every input class, generous destination. **This table is the fix for the ASCII-only one
it replaces.**

| class | 64 | 512 | 4000 | 32000 |
|---|---:|---:|---:|---:|
| ascii-lower | 7.30× | 9.85× | 10.11× | 10.32× |
| ascii-mixed | 6.81× | 9.60× | 9.81× | 10.01× |
| latin-1 | 4.38× | 4.49× | 4.62× | 4.67× |
| cyrillic | 2.17× | 2.13× | 2.17× | 2.17× |
| CJK (the lookup with nothing to show for it) | 2.14× | 2.13× | 2.17× | 2.17× |
| mixed ASCII + latin-1 | 3.16× | 3.20× | 3.28× | 3.30× |

**Overall geomean 4.225× faster over 24 rows. Worst class 2.13×. No size class regressed → LANDS.**

| | before | after |
|---|---:|---:|
| geomean, 24 rows | 1.900× | **4.225×** |
| worst row | 0.59× | **2.13×** |

### A measurement artefact worth recording

In the survey both 64 KB maps are linked into one process, and 027 measured 14151 ns where 031
measured 8659 ns on identical code. Measured alone — which is what each change's own bench does —
the two are within noise of each other at ~8675 ns. Two 64-kilobyte tables do not both stay in L2.
The survey's ratios are therefore the pessimistic ones, and they are the ones quoted above.

## Reproduce
```
changes\027-rtlupcaseunicodetomultibyten\build.bat
```
