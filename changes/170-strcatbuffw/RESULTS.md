# 170 `shlwapi!StrCatBuffW` — **LANDS** (3.59× geomean, up to 9.87×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

## Why this target

From the survey of unconverted shlwapi/kernelbase exports: `StrCatBuffW` costs **109 ns** to append
a 254-char string — scalar in both of its phases (the length scan and the copy).

## The contract (derived, then fuzz-confirmed — `probes/scb.c`)

```
PWSTR StrCatBuffW(PWSTR dst, PCWSTR src, int cchDestBuffSize)
    n    = wcslen(dst)                  <- UNBOUNDED; the bound does not clamp this scan
    room = cchDestBuffSize - n
    room > 0  -> copy min(room-1, wcslen(src)) chars at dst+n, then exactly ONE NUL
    room <= 0 -> dst is left COMPLETELY UNTOUCHED, existing terminator intact
    returns dst always
```

So it is precisely `StrCpyNW(dst + wcslen(dst), src, cch - wcslen(dst))` — which is why this change
shares change 168's fused copy verbatim. Confirmed against the live export on the **first attempt:
2 000 000 cases, 0 mismatches**.

Two points worth keeping:

* **`cchDestBuffSize` is the size of the WHOLE buffer, not the room remaining** — `StrCatBuffW(d,
  L"de", 6)` on `d = "abc"` yields `"abcde"` exactly filling 5 chars + NUL.
* **The no-room case writes nothing at all.** With `dst = "abcdef"` and a bound of 4 — already below
  `wcslen(dst)` — the buffer is untouched; it is *not* truncated to the bound. The subtraction is
  signed, so a short or negative bound cannot wrap into "huge".

## Method

Two vector passes, no scalar loop on the hot path: an inline AVX2 `wcslen` over `dst` (aligned down
and masked, so no `call` and no page hazard), then change 168's fused scan-and-copy for the append,
which traverses `src` once and stops at whichever comes first — terminator or budget.

### The short-`dst` probe (what made this land)

The first cut **PARKED at 0.90×** on the *empty-dst + 16-char-src* class. Appending to an empty or
very short buffer is the ordinary "build a string up" idiom, and there the vector length scan costs
more than it saves: a `load → vpcmpeqw → vpmovmskb → shr → branch` chain is ~9 cycles of pure
latency just to discover a length of 0. A SWAR has-zero over a single 8-byte load answers it in ~4,
and `tzcnt >> 4` converts the bit index straight to a character index. That class went
**0.90× → 1.04×** with nothing else moving.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**, comparing the *whole* destination buffer:
dst 0..80 × src lengths × every bound from −2 upward; the **no-room case verified to leave dst
byte-identical**; 16 unaligned dst start offsets; low-byte collision sweep (`0x0141`/`0x4101`);
250 000 randomized cases over the full code-unit range; and a **NOACCESS page guard** with `src`
ending exactly at a page boundary across every bound.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| empty + 16 | 9.38 | 9.79 | 1.04× |
| empty + 254 | 18.36 | 108.57 | 5.91× |
| 64 + 16 | 11.71 | 28.72 | 2.45× |
| 64 + 254 | 23.10 | 122.21 | 5.29× |
| 254 + 254 | 27.31 | 172.21 | 6.30× |
| 254 + 1024 | 48.41 | 477.78 | **9.87×** |
| 64 + 254, bound 80 | 18.55 | 28.56 | 1.54× |

**geomean 3.589×**

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
