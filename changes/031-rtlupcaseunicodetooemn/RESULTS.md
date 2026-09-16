# 031 — `RtlUpcaseUnicodeToOemN` (AVX2) — **LANDED** (core ntdll, 4.19× geomean over every input class)

Raw counted upcase-and-narrow to OEM (UTF-16 → OEM, upcasing). Combines 015 upcase with the OEM narrow.

- **Contract:** `NTSTATUS RtlUpcaseUnicodeToOemN(char* dst, ULONG maxBytes, PULONG outLen,
  const wchar_t* src, ULONG srcBytes)`. Reports `STATUS_BUFFER_OVERFLOW` on truncation.
- **Compared against:** live `ntdll.dll!RtlUpcaseUnicodeToOemN`. **ISA:** AVX2.
- All-ASCII 16/8-wide blocks upcase a–z in-register then pack; non-ASCII via `wia_upoemmap[]`
  (upcase∘OEM codepage) from the OS.

## Correctness — PASS

Output + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + non-ASCII) and truncation.

## The table used to be `L'a' + (k & 15)` and nothing else — 2026-09-16

For an upcase conversion with a **table path**, that is the wrong table to publish. ASCII is the one
case the vector block handles; every character above `0x7F` goes through the 65536-entry map, and
that path had never been timed. `discovery/upcase_nonascii_rows.c` (commit `2e3ca3b`) asked, and got
**0.59× on Cyrillic or CJK** — geomean 1.850× over 24 rows, which is PARKED.

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
| ascii-lower | 6.89× | 10.01× | 10.34× | 10.27× |
| ascii-mixed | 6.81× | 9.60× | 9.77× | 9.36× |
| latin-1 | 4.22× | 4.28× | 4.47× | 4.44× |
| cyrillic | 2.13× | 2.11× | 2.19× | 2.17× |
| CJK (the lookup with nothing to show for it) | 2.12× | 2.14× | 2.19× | 2.19× |
| mixed ASCII + latin-1 | 3.24× | 3.29× | 3.41× | 3.38× |

**Overall geomean 4.193× faster over 24 rows. Worst class 2.11×. No size class regressed → LANDS.**

| | before | after |
|---|---:|---:|
| geomean, 24 rows | 1.850× | **4.193×** |
| worst row | 0.59× | **2.11×** |

### A measurement artefact worth recording

In the survey both 64 KB maps are linked into one process, and 027 measured 14151 ns where 031
measured 8659 ns on identical code. Measured alone — which is what each change's own bench does —
the two are within noise of each other at ~8675 ns. Two 64-kilobyte tables do not both stay in L2.
The survey's ratios are therefore the pessimistic ones, and they are the ones quoted above.

## Reproduce
```
changes\031-rtlupcaseunicodetooemn\build.bat
```
