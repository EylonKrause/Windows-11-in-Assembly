# 175 `shlwapi!PathRemoveArgsW` — **LANDS** (6.34× geomean, up to 17.71×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

## Why this target

The slowest thing left in the survey with a derivable contract: **215 ns** for a 254-char path.

## The contract — three behaviours, and the obvious rule is wrong

Derived across two probe rounds (`probes/pra.c`, `probes/pra2.c`) and fuzz-confirmed bit-exact
against the live export over **2 000 000 cases**. The natural reading — "terminate at the first
unquoted space" — is **refuted on 265 039 of 2 000 000 cases**. Finding out why needed a probe that
prints which *cells* were written, not just what the string looks like afterwards.

**1. The split character is exactly U+0020.** Swept over all 65535 code units, exactly one splits.
**A tab does not** — `"prog.exe<tab>arg"` comes back untouched.

**2. It writes more than one cell.** The space before the arguments is NULled, *and so is the last
space of that run* when a non-space follows:

| input | cells written |
|---|---|
| `ab c` | 2 |
| `ab  c` | 2 **and 3** |
| `ab   c` | 2 **and 4** — the *last* space of the run, not the second |
| `ab    c` | 2 **and 5** |
| `ab  ` | 2 only — nothing follows the run |

**3. With no unquoted space at all, it falls back to trimming trailing blanks.** This resolves a
pair that looks self-contradictory until you see it:

| input | result | why |
|---|---|---|
| `"` + `' '` | the space **is** cut | no unquoted space exists, so the trailing trim applies — even though the space sits inside a quote |
| `"` + `' '` + `a` | nothing is cut | the space is neither unquoted nor trailing |

and the trim removes the **whole** trailing run, terminating at its first character: `"` + `"  "`
writes cell 1, not cell 2.

Quoting itself is a plain toggle: `"ab" c` splits at the space (quotes closed), `a" "b` does not
(space inside), `a"b c"d e` splits at the *second* space only.

## Method

An **event scan**. One vector pass looks for the first character that is any of
{terminator, U+0020, `"`} — three `vpcmpeqw` results OR-ed into a single mask, so one `tzcnt`
locates it — and only those events drive the quote state machine. Real paths contain few spaces and
almost never a quote, so the inherently sequential part costs almost nothing while the skipping runs
16 characters at a time. Everything after the scan is a short scalar walk.

Two of the three constants are taken as **VEX memory operands** rather than registers: only
`ymm0`–`ymm5` are volatile under the Win64 ABI, and data + three constants + three results would not
fit.

**Page safety:** every 32-byte load is issued only when `(cursor & 4095) <= 4064`. Within 32 bytes of
a page end it tests one character and retries.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**, comparing the whole buffer — which matters
more here than usual, since this function writes multiple cells and leaves the argument text sitting
behind the terminator:

- every probe-derived case, including all the ones that refuted the simple rule
- **exhaustive** over `{a, space, ", .}` to length 9 — 349 525 strings
- **all 65535 code units** mid-string (pins the split set) **and** trailing (pins the trim set)
- space runs of length 1..4 at many positions × 16 unaligned start offsets × lengths 4..120
- fully quoted long paths with an internal space (which must be protected end to end)
- 400 000 randomized cases over a quote/space-heavy alphabet
- **NOACCESS page guard**, with no space (forcing the event scan to the very edge) and with a
  trailing space right at the edge

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| args @8 | 11.73 | 18.36 | 1.57× |
| args @250 | 18.25 | 242.27 | **13.27×** |
| no space / 254 | 17.86 | 215.79 | **12.08×** |
| no space / 1024 | 49.12 | 870.18 | **17.71×** |
| fully quoted / 254 | 22.33 | 194.25 | **8.70×** |
| realpath (quoted exe + quoted arg) | 19.00 | 31.98 | 1.68× |

**geomean 6.343×**. The win is largest exactly where the shipped code is weakest — when it has to
walk the whole string, whether because the arguments are far in, because there are none, or because
every space is quoted.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
