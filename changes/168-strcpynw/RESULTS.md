# 168 `shlwapi!StrCpyNW` — **LANDS** (5.01× geomean, up to 12.65×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117,
ml64/cl 14.50.35717. First change developed on the second PC.

## Why this target

Chosen by surveying every shlwapi / kernelbase export not yet converted and timing the shipped
implementation on a 254-char path. `StrCpyNW` costs **108 ns to copy 254 characters — ~4.7 GB/s**,
which is a character-at-a-time scalar loop on a core that will do 60+ GB/s.

## The contract (derived, then fuzz-confirmed — `probes/scn.c`)

```
PWSTR StrCpyNW(PWSTR dst, PCWSTR src, int cchMax)
  cchMax <= 0 -> writes NOTHING AT ALL (not even a terminator); returns dst
  otherwise   -> copies min(cchMax-1, wcslen(src)) chars, then exactly ONE NUL
  returns dst always
```

Two findings worth keeping:

* **It is `strlcpy`-shaped, not `strncpy`-shaped.** There is no NUL fill of the remainder —
  `StrCpyNW(d,L"ab",10)` writes exactly `a`,`b`,NUL and leaves the other seven slots untouched.
  A `strncpy`-style implementation would be bit-wrong on every short copy.
* **`cchMax` is signed and compared signed.** `cchMax` of 0, −1 or −100 writes *nothing* — not
  even a terminator — so a negative value does not wrap into "huge". Reproduced exactly.

The candidate reference was confirmed against the live export on the **first attempt: 2,000,000
fuzz cases, 0 mismatches**, including every `cchMax` from −4 upward and sources up to 60 chars.

## Method

One **fused scan-and-copy** pass — the string is traversed once, with no separate `wcslen`.
Each iteration loads 32 bytes of src, tests for a terminator with `vpcmpeqw`, and stores all 32
bytes to dst only if the block is terminator-free *and* at least 16 characters of budget remain.
The moment a terminator comes into range, or the budget drops below 16, a scalar tail finishes.

**Page safety.** The 32-byte source load is issued only when `(src & 4095) <= 4064`, proving the
read stays inside src's own page — a page that must be mapped, since the characters already copied
came from it. Within 32 bytes of a page end the code copies a *single* character and re-tests, so
it creeps across the boundary and then resumes vector speed, instead of degrading to scalar for the
rest of the string. The 32-byte destination store happens only while the budget is ≥ 16 characters,
so it can never write past `cchMax-1`.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle **and the live `shlwapi` export**, comparing the *whole*
destination buffer (so any write past the terminator is caught):

- every length 0..200 × every `cchMax` from −3 to len+3, plus a generous bound
- non-ASCII sources across the code-unit range
- **low-byte collision sweep** (`0x0141` vs `0x4101`): a byte-wise copy would pass a naive check
  here and fail this one
- 300 000 randomized cases over the full code-unit range with `cchMax` including 0 and negatives
- **NOACCESS page guard**: source ending exactly at a page boundary, terminator slid across the
  last 80 characters, every `cchMax` — an over-read faults the process

## Gate 2 — speed: **LANDS**, no size class regressed

| size | ours ns | system ns | ratio | ours GB/s |
|---|---|---|---|---|
| 8 | 3.22 | 7.05 | 2.19× | 4.97 |
| 16 | 2.20 | 10.87 | 4.93× | 14.52 |
| 64 | 3.42 | 29.76 | 8.69× | 37.39 |
| 254 | 10.89 | 109.52 | **10.05×** | 46.63 |
| 1024 | 32.69 | 413.53 | **12.65×** | 62.65 |
| realpath (49) | 5.84 | 21.82 | 3.74× | 16.78 |
| truncate 254→16 | 4.63 | 8.26 | 1.78× | 6.48 |

**geomean 5.012×** — and note the `truncate-254to16` row: the bound is hit long before the
terminator, which is the case a scan-then-copy implementation would waste a full `wcslen` on. The
fused pass stops at the bound, so it wins there too.

## ISA and portability

AVX2 only — **no AVX-512, no GFNI**, even though this bench has both. The implementation is
correct on the 5950X as well, so this change is not second-PC-specific.
