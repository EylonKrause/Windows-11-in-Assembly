# 142 `PathAddBackslashW` — 2ND PC (Zen 4) re-validation → remains **PARKED**, no variant shipped

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

This change was PARKED on the 5950X and is **still parked here, for the same class**. Two fixes were
written and measured; **neither landed, so neither was kept** — there is deliberately no `impl_2ndpc.asm`
in this directory. Recorded so the attempts are not repeated.

## Baseline on this machine

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 16 | 10.35 | 9.12 | **0.88×** | WORSE ← gate failure |
| 64 | 10.69 | 20.24 | 1.89× | BETTER |
| 254 | 13.17 | 58.84 | 4.47× | BETTER |
| 1024 | 33.82 | 225.15 | 6.66× | BETTER |
| realpath | 26.81 | 215.75 | 8.05× | BETTER |

geomean 3.316×. Stable across three repeat runs: 0.88× / 0.88× / 0.88×.

## Attempt 1 — 64-byte first probe. **Rejected: 0.87×**

A 16-character path needs two 32-byte probes: the first load is aligned down and covers only characters
0..15, while the terminator sits at character 16. Replacing them with a single page-guarded 64-byte probe
(two independent loads, one dependency chain) gave **0.87× at size 16**, geomean 3.268×. So the cost is
**not** the second movemask chain.

## Attempt 2 — scalar short path, per change 164's remedy. **Rejected: 0.89×, and it hurt the rest**

Change 164 (`PathCchAddBackslash`) records that *"paths ≤23 chars never touch a vector register — a 2-byte
load forwards from a caller's recent write where a 32-byte load cannot."* Applying that here — a
word-at-a-time terminator scan for the first 32 characters, completing without any vector register —
measured:

| case | ratio | vs original |
|---|---|---|
| 16 | 0.89× | still fails |
| 64 | 1.12× | **down from 1.89×** |
| 254 | 2.83× | **down from 4.47×** |
| 1024 | 5.22× | **down from 6.66×** |

geomean 2.492× — worse overall, because paths longer than 32 characters now pay 32 wasted scalar
iterations first.

## Conclusion

Both hypotheses are refuted by measurement. For a 16-character path shlwapi's own short scan is already
near-optimal and the remaining gap (~1.4 ns, much of which the bench's shared `memcpy` masks) is not
recoverable by either widening or narrowing our scan. The original `impl.asm` is the best of the three and
remains the implementation of record; the change stays PARKED on both machines.
