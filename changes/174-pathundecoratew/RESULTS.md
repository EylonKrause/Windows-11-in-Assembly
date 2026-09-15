# 174 `shlwapi!PathUndecorateW` — **LANDS** (3.90× geomean, up to 8.02×)

> ## CORRECTED 2026-09-15 — this change had shipped WRONG
>
> Conjunct **(b)** of the contract below — the group's `]` must sit immediately before the **last
> `.` of the component** — is an extension position by another name, and it carried the same gap
> that change [132 `PathFindExtensionW`](../132-pathfindextensionw/) shipped with: **a SPACE stops
> the extension scan exactly as a backslash does.**
>
> This change never cited 132. It derived its own four-conjunct rule from scratch and fuzz-confirmed
> it over 2 000 000 cases — over an alphabet with **no space in it**, which is exactly why neither
> its own corpus nor the first audit of the 132 bug (which looked at the changes that *mention* 132,
> and corrected 140, 143 and 144) could see it. A second, **structural** sweep —
> `discovery/extension_space_audit2.c`, every landed oracle that computes an extension position
> whether or not it says where the rule came from — found this one, along with 158, 159 and 160.
>
> The smallest failing case is `". []"`: the live export undecorates it to `". "`, this
> implementation left it alone.
>
> | over every string of `{a, '.', \, '[', ']', SPACE}` of length 0..7 | mismatches |
> |---|---|
> | live `PathUndecorateW` vs the rule as landed | **1 634** of 335 923 |
> | live `PathUndecorateW` vs the corrected rule | **0** |
>
> **The two uses of the backslash had to be separated.** It was doing double duty here: delimiting
> the COMPONENT for conjunct (d) — the `[` may not be the component's first character — and
> bounding the extension search for conjunct (b). **Only the second takes the space.** The forward
> scan therefore tracks two positions now, in `r10` and a pushed `rbx`: `comp`, just past the last
> backslash, and `stop`, just past the last backslash **or space**. The final test changed from
> `cmp r11, r10` to `cmp r11, rbx`. Verified protective: reverting just that one compare makes the
> new corpus fail immediately.
>
> **And the correction made it faster.** The fourth compare per block cost real throughput
> (1024-char: 81.0 → 98.9 ns) until the loop was restructured around it: the overwhelmingly common
> block contains *none* of `\`, `.`, ` ` or NUL, and one `vpor` + one `vpmovmskb` now answers that
> for all three at once instead of three separate mask/test/branch triples. That is **fewer** uops
> per block than the loop ran before the space stopper existed — 1024-char is now **65.1 ns**,
> better than the 81.0 ns this change originally shipped, and the peak went 6.61× → **8.02×**.
>
> `correctness.c` now also enumerates every string over `{[, ], ., 1, SPACE, z}` to length 7.

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
| 16 / decorated | 9.94 | 17.64 | 1.78× |
| 64 / decorated | 17.99 | 45.52 | 2.53× |
| 254 / decorated | 24.67 | 144.31 | 5.85× |
| 1024 / decorated | 65.11 | 522.37 | **8.02×** |
| 254 / no decoration | 21.12 | 138.54 | 6.56× |
| realpath | 13.20 | 33.69 | 2.55× |

**geomean 3.902×** — re-measured after the correction. Faster than the numbers this change
originally shipped at every size from 254 up, for the reason given in the correction note.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
