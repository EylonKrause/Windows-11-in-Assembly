# 123 — `ntdll!RtlFindLongestRunClear` — **LANDS** (4.79× geomean, up to 30×)

Finds the longest run of **clear (0) bits** in an `RTL_BITMAP`, returns its length, and writes its start
bit index to `*StartingIndex`. ntdll's routine scans **bit-by-bit** (~0.87 cycles/bit — ~11.5 µs on a
64 Kb bitmap); this walks 64-bit words with an AVX2 all-zero-chunk skip and beats it at every size and
density. A fresh vein after the IP/MAC/GUID parse-side family: the **highest-headroom untouched scalar
routine** on this machine (measured 12.6 µs/call live on an 8 KB sparse bitmap).

## Contract (matched bit-exact vs live: return length + `*StartingIndex`)
Reverse-engineered reference-first against the live export (length **and** `*StartingIndex`), confirmed
on the first oracle over edges + 3M fuzz:
- returns the length of the **earliest** longest clear run (ties break to the lowest start index);
- writes that run's start bit to `*StartingIndex`;
- an **all-set or empty** (`SizeOfBitMap == 0`) bitmap returns length **0** with `*StartingIndex = 0`;
- bits at index ≥ `SizeOfBitMap` are ignored — they bound the run at `SizeOfBitMap` (the last partial
  word is masked with its high bits forced set).

## Method
Word-at-a-time (64-bit) state machine tracking the running cross-word clear-run:
- **all-clear word** → extend the run by 64; an **AVX2 `vptest`** bulk-skips 256-bit all-zero chunks
  (the common case on allocation bitmaps with long free runs) — one `vmovdqu`+`vptest` per 4 words;
- **all-set word** → close the run (update best), reset;
- **mixed word** → merge the incoming run with the word's low clear-run (`tzcnt`), scan the **interior**
  for its longest clear-run, then start a new run from the high clear-run (`lzcnt`).

The interior scan picks the cheaper of two O(k) methods **per word by popcount**: loop over set bits
(k = popcount) when the word is sparse, else shift-AND `s &= s>>1` (k = the run length) when dense — so
per-word cost is ~`min(popcount, longest-run) ≤ 32` regardless of density. This is what lets it win on
**both** sparse and dense bitmaps (a single method regresses on one or the other — measured).

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. Length and `*StartingIndex` match the live export and an independent
bit-by-bit oracle on all-clear/all-set sweeps (0–512 bits), single-set-bit and single-clear-bit position
sweeps, the empty bitmap, plus **5 000 000 fuzz** bitmaps across densities and lengths (runs spanning
words and runs wholly inside one word).

## Benchmark — vs live `ntdll!RtlFindLongestRunClear`
geomean **4.79×** (2.18×–**30.4×**).

| bitmap | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 256 bits, sparse | 16.7 | 38.0 | 2.28x |
| 4 Kb, sparse | 279 | 627 | 2.24x |
| 64 Kb, sparse | 5131 | 11192 | 2.18x |
| 4 Kb, dense | 268 | 1317 | 4.91x |
| 64 Kb, dense | 4531 | 32984 | 7.28x |
| 64 Kb, all-clear | 180 | 5470 | **30.44x** |

## Scope / next
`RtlFindLongestRunClear`. The related bitmap run-finders measured on this machine also have large
headroom and are natural follow-ups: `RtlFindSetBits`/`RtlFindClearBits` (find N consecutive, with a
hint + wraparound contract) and `RtlNumberOfClearBits` (AVX2 popcount, the complement of the landed
[023](../023-rtlnumberofsetbits/)).

## Reproduce
```
changes\123-rtlfindlongestrunclear\build.bat
```
