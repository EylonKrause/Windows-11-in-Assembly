# 099 — `strncmp` (bounded byte compare) — **PARKED** (loses one mid-size class)

## Verdict
The shipped `ucrtbase!strncmp` is a tuned **SWAR** byte loop: it aligns one pointer, then
reads 8 bytes at a time with the classic `0x7efe…`/`0x8080…` "has-zero" trick — and because
it aligns first, an aligned 8-byte read never crosses a page, so it needs **no per-read page
check** and stays branch-light. That makes it very fast at small sizes.

Our AVX2 bounded compare **wins the large sizes** (128 B–32 KB: 1.46×–2.06×, ~22–25 GB/s vs
ucrtbase's ~7 GB/s), **ties at 8 bytes** (a no-YMM SWAR-8 path with a final-chunk shortcut),
but **loses at 32 bytes** (0.91×): a single 32-byte `vpcmpeqb` needs a `vzeroupper`, and a
16-/8-byte fallback needs the per-read page checks ucrtbase's alignment avoids — so 32 bytes
lands just under ucrtbase's four aligned SWAR reads. Every other approach tried (xmm-only VEX
loop, SWAR-8×4) was slower still at 32. One size class regresses → PARKED.

## Benchmark — vs live `ucrtbase!strncmp` (`/Od`, equal strings, full-length compare)
| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 8 | 3.12 | 3.12 | 1.00x (~tie) |
| 32 | 4.91 | 4.46 | **0.91x** |
| 128 | 7.58 | 11.08 | 1.46x |
| 512 | 20.5 | 42.2 | 2.06x |
| 4096 | 190 | 314 | 1.65x |
| 32000 | 1454 | 2502 | 1.72x |

geomean 1.41× overall, but the strict all-classes gate parks it on the 32-byte class.

## Correctness — behavior-identical (sign) vs live ucrtbase + oracle
`correctness.exe`: **PASS**. strncmp's contract defines only the SIGN of the result (and
ucrtbase is itself inconsistent — full byte-difference in its scalar prefix, normalized sign
in its SWAR bulk); ours returns the full difference, always the right sign, matching the
oracle exactly and the live export in sign. Fuzz: len 0..300 × 8 alignments × n {0..301}
with planted differences / early terminators, plus a NOACCESS page-guard on both operands.

## Why kept
A genuine large-input win and a clean two-regime (no-YMM small / AVX large, page-safe,
resumes the vector loop after a page-cross byte step) implementation; recorded honestly
alongside the other tuned-incumbent parks. A future image could route ≥128-byte compares
through this and keep ucrtbase below that.

## Reproduce
```
changes\099-strncmp\build.bat
```
