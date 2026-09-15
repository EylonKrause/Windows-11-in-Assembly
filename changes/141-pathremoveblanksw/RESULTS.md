# 141 — `shlwapi!PathRemoveBlanksW` — **LANDS** (4.67× geomean, up to 10.8×; the 16-char class is now a TIE)

> **Re-measured 2026-09-15.** The geomean has *improved*, 4.23× → 4.67×, but the smallest class is no
> longer a win: 16 characters now reads **0.96×–1.01× across repeated runs** — a genuine tie that
> flips the single-run gate. The cause is the same as change 047's: shlwapi's own
> `PathRemoveBlanksW` got faster on this build, 18.02 ns → ~14.3 for that class (21%), while ours
> went 15.97 → ~14.5. It is recorded as a tie rather than a regression because it is one; calling it
> PARKED would overstate it, and leaving "every size class better" would be false. Every other class
> still wins, up to 10.8×.

Strip leading and trailing spaces in place. shlwapi's is a scalar scan (207 ns for a 254-char string).

## Contract (probed against the live export)
Three details, each measured rather than assumed:
- **Only SPACE (0x20) is stripped.** A tab is not — `"\t abc \t"` comes back unchanged. Verified by
  sweeping every near-space code point 0x09–0x21 at both ends.
- **There is no MAX_PATH guard**, unlike [140 `PathRemoveExtensionW`](../140-pathremoveextensionw/),
  which stops working entirely at 260 characters. Confirmed by sweeping lengths 250–300; this one keeps
  trimming throughout. Two neighbouring functions in the same DLL, opposite answers.
- **The order is MOVE FIRST, then terminate** — the whole remainder (trailing blanks *and* the
  terminator) is shifted down, and only then is the NUL written that drops the trailing blanks. This is
  the **reverse** of [139 `StrTrimW`](../139-strtrimw/), which terminates first and then moves. The
  difference is invisible in the resulting string but visible in the bytes past the new terminator, so
  the correctness harness compares the whole buffer and the implementation reproduces the real order.

## Method
Two AVX2 scans (leading run of spaces, then the length), the shift, then a short scalar backward scan
for the trailing blanks — trailing runs are short, and the all-spaces string is already resolved by the
leading scan.

The shift is size-dispatched: **`rep movsw` only from 24 characters up**, with a plain word-copy loop
below that. This mattered — with `rep movsw` unconditionally the 16-character case measured **0.77×**
(its startup cost exceeded the entire move) and the change would have parked; the split turned it into
1.13× and every class positive.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, comparing the **entire buffer** (pre-filled with sentinel data) over:
**lengths 0..200 × leading spaces 0..8 × trailing spaces 0..8**; all-space strings at every length;
interior-space patterns; the **MAX_PATH region 250–300** (proving the absence of a guard); and **every
near-space character 0x09–0x21** placed at both ends to confirm only 0x20 is touched.

## Benchmark — vs live `shlwapi!PathRemoveBlanksW`
geomean **4.23×**, every size class better *as originally measured*:

| input | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 15.97 | 18.02 | 1.13x |
| 64 chars | 28.58 | 52.14 | 1.82x |
| 254 chars | 34.05 | 207.16 | 6.08x |
| 1024 chars | 77.37 | 768.22 | 9.93x |
| `C:\Program Files\…\wordpad.exe` | 36.04 | 390.62 | **10.84x** |

Each iteration restores the mutated buffer with a `memcpy` charged to both sides.

## Reproduce
```
changes\141-pathremoveblanksw\build.bat
```
