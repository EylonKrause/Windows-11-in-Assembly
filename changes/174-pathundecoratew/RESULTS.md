# 174 `shlwapi!PathUndecorateW` — **LANDS** (3.72× geomean, up to 6.61×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

## Why this target

From the survey of unconverted shlwapi/kernelbase exports: **134 ns** for a 254-char path — the
second-slowest of everything with a derivable contract.

## The contract (derived over three rounds — `probes/pud.c`)

`PathUndecorateW` turns `file[1].txt` into `file.txt`. The decoration is removed only when **all
four** of the following hold, and each was established by probe, not assumed:

1. **Last component only** — after the last backslash. `C:\dir[1]\file.txt` is left alone.
2. **The group must hug the extension.** Its `]` has to be the character immediately before the
   **last** `.` of that component, or immediately before the end of the string when there is no `.`.
3. **Contents are decimal digits, and there may be none.** `file[].txt` → `file.txt`, but
   `file[a].txt`, `file[1a].txt`, `file[-1].txt` and `file[ ].txt` are all untouched.
4. **The `[` may not be the component's first character.** `[1].txt` and `x\[1].txt` are left alone;
   `file[1].txt` and `x\a[1].txt` are not.

Rule 2 is the one that matters and the one that is easy to get wrong. The obvious reading —
"remove the first bracket group that is followed by `.` or the end" — is **refuted**:

| input | live result | why |
|---|---|---|
| `a[1].b[2]` | `a.b[2]` | the group before the only dot |
| `a[1].b[2].c` | **`a[1].b.c`** | the group before the **last** dot — *not* the first qualifying one |
| `a[1]x[2]` | `a[1]x` | no dot, so the group before the end |
| `file[1][2].txt` | `file[1].txt` | only `[2]` hugs the dot |

Two candidate references were refuted on the way (171 989 and then 29 053 mismatches of 2 000 000)
before the extension-anchored rule passed **2 000 000 / 2 000 000**.

Like the shipped function, the removal closes the gap by moving the remainder down and **leaves the
stale tail past the new terminator untouched** — the correctness test compares that tail too.

## Method

**One forward pass** computes everything the rule needs — the length, the last backslash and the
last dot — from three `vpcmpeqw` results per 32-byte block. Tracking the *last* match rather than
the first is why this is a forward pass using `bsr` on each mask, in the style of change 149.
Because only `ymm0`–`ymm5` are volatile under the Win64 ABI and three broadcast constants plus the
data plus three results would need seven registers, the backslash and dot constants are taken
directly as **VEX memory operands** instead of occupying registers.

Everything after the scan is a short backward digit walk and one forward vectorised move (the
destination is strictly below the source, so forward is safe despite the overlap).

Two bugs were caught before the first build: an unsigned compare that would have mistaken the `-1`
"no dot" sentinel for a huge offset, and a `jb`/`jbe` boundary that could have let the digit walk
underflow past the start of the buffer.

**Page safety:** every 32-byte load is issued only when `(cursor & 4095) <= 4064`, proving the read
stays inside the cursor's own page. Within 32 bytes of a page end it steps one character and retries.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**, comparing the whole buffer *including the
stale tail*:

- every probe-derived case explicitly
- **exhaustive** over `{a, [, ], 1, ., \}` to length 8 — 1 727 604 strings, which drives every branch
- **all 65535 code units inside the brackets** (pins "digits only, possibly none")
- **all 65535 code units immediately after the `]`** (pins "must be `.` or the terminator")
- the decoration at many positions × 16 unaligned start offsets × lengths 6..120
- 400 000 randomized cases
- **NOACCESS page guard**, both with no decoration (forcing a scan to the very edge) and with a
  decoration at the edge

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 16 / decorated | 9.95 | 17.86 | 1.80× |
| 64 / decorated | 18.44 | 46.42 | 2.52× |
| 254 / decorated | 30.45 | 147.88 | 4.86× |
| 1024 / decorated | 81.00 | 535.48 | **6.61×** |
| 254 / no decoration | 22.02 | 143.14 | **6.50×** |
| realpath | 12.36 | 34.52 | 2.79× |

**geomean 3.716×**

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
