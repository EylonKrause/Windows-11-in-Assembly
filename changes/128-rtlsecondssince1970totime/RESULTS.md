# 128 — `ntdll!RtlSecondsSince1970ToTime` — **LANDS** (2.00×)

Convert Unix seconds to a 64-bit Windows time (100-ns units since 1601-01-01). Joins the landed time
family ([126](../126-rtltimetotimefields/), [127](../127-rtltimefieldstotime/)).

## Contract (matched bit-exact vs live)
`Time = (ElapsedSeconds + 11644473600) * 10000000`, where 11644473600 = 134774 days × 86400 = the
seconds between 1601-01-01 and 1970-01-01. `ElapsedSeconds` is a `ULONG`, so no overflow is possible:
the largest input yields 1.59e17, far inside a positive int64.

## Method
ntdll spends ~30 cycles here. The whole computation is a zero-extend, a multiply and an add — four
instructions. The epoch offset is folded by distributing the multiply:

```
(s + 11644473600) * 1e7  ==  s*1e7 + 116444736000000000
```

which also sidesteps the fact that `add r64, imm` only accepts a sign-extended **imm32** (11644473600
does not fit, but 10000000 does), so no extra constant load is needed on the multiply.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. Matches the live export and an independent oracle on edges (0, 1, `ULONG_MAX`,
2^31 either side), **every power-of-two boundary** (`2^k − 1`, `2^k`, `2^k + 1` for k = 0..31), a
**2 000 000-value dense low sweep**, and a **4 200 000-point stride across the entire ULONG domain**.

## Benchmark — vs live `ntdll!RtlSecondsSince1970ToTime`
geomean **2.00×**:

| input | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 0 (epoch) | 2.67 | 6.29 | 2.36x |
| 1700000000 (2023) | 3.33 | 6.29 | 1.89x |
| 2^31 | 3.33 | 6.29 | 1.89x |
| 4294967295 (max) | 3.33 | 6.29 | 1.89x |

## Note on the inverse
`RtlTimeToSecondsSince1970` was measured at **1.33 ns** (~6 cycles) — already essentially optimal, with
no headroom worth a change. Not attempted; recorded here so the omission is deliberate rather than an
oversight.

## Reproduce
```
changes\128-rtlsecondssince1970totime\build.bat
```
