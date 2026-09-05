# 089 — `strstr` (byte substring search) — **PARKED** (ties/loses below ~2 KB)

## Verdict
The shipped `ucrtbase!strstr` is **not scalar** — it is a tuned **SSE4.2 `pcmpistri`**
substring scan (the disassembly shows `66 0f 3a 63 c8 0c` = `pcmpistri xmm1,xmm0,0x0c`
"equal ordered" with 4 KB page-cross guards). It runs at ~22 GB/s and has a very small
per-call cost. `wcsstr` is the same (`pcmpistri` imm `0x0d`, word granularity).

Our hand AVX2 **two-char anchor** (Muła: broadcast needle[0] and needle[m-1], mask both
per 32-byte block, verify the middle) streams at **~28 GB/s** and **beats `pcmpistri` on
large inputs** — but `pcmpistri`'s low fixed cost wins on small/medium, so a size class
regresses and the change cannot LAND under the project's all-classes gate.

## Benchmark — vs live `ucrtbase!strstr` (`/Od`, 4-char needle planted at the end)
| size | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 9.6 | 4.2 | 0.44x |
| 32 | 6.7 | 4.9 | 0.73x |
| 128 | 10.4 | 8.9 | 0.86x |
| 512 | 30.1 | 26.1 | 0.87x |
| 4096 | 162.8 | 194.9 | **1.20x** |
| 32000 | 1137.6 | 1447.6 | **1.27x** (28.1 GB/s vs 21.9) |

Crossover is ~2 KB. Two implementations were tried: a 1-block-lookahead pipeline (single
load/block; best large-size numbers, shown above) and a boundary-step variant (no pipeline
drain but per-block boundary bookkeeping cost it the large-size lead). Neither clears the
small/medium classes against `pcmpistri`.

## Correctness — bit-exact vs live crypt… `ucrtbase!strstr` AND the scalar oracle
`correctness.exe`: **PASS**. Fuzz haystack len 0..400 × 8 alignments × needle {0..40}
(random / guaranteed-present / definitely-absent) + empty needle + a NOACCESS page-guard
(needle whose first char matches at the tail and would overrun; needle longer than the
remaining suffix). The kept impl is the pipelined two-char anchor.

## Why kept
Recorded honestly alongside the other pcmpistri/VPCLMULQDQ-incumbent parks (`memcmp`,
`crc32`). The AVX2 core is a genuine large-input win and documents that Windows' `strstr`
family is already hardware-string-instruction optimized — so a future image keeps the
shipped code below ~2 KB and could switch to this streamer only for very long haystacks.

## Reproduce
```
changes\089-strstr\build.bat
```
