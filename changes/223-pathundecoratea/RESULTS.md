# 223 `shlwapi!PathUndecorateA` — **LANDS** (27.05× geomean, up to 81.6×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min-of-300, pinned core, lengths computed rather than hardcoded.

## Why this target

From `discovery/shlwapi_narrow2.c`, the survey of the twelve narrow shlwapi siblings still
unconverted: **183.31 ns** against **41.14 ns** for `PathUndecorateW` on the same character count —
**4.46×** the wide cost for **half the bytes**, the best remaining ratio after `PathRemoveBlanks`
(change [221](../221-pathremoveblanksa/)). The `A` exports in this DLL are not thin wrappers; they
are MBCS-aware per-character walks, and that is the whole gap this change closes.

The measured gap turned out to be far wider than the survey suggested. On the bench's 254-character
case the live export takes **767 ns**, not 183 — the survey's shapes were shorter and less
decorated. Against that, this implementation runs in 16.2 ns.

## The rule — and the bug it uncovered in the WIDE sibling

`PathUndecorateA` turns `file[1].txt` into `file.txt`. Change [174](../174-pathundecoratew/) derived
the contract for the wide form and fuzz-confirmed it over 2 000 000 cases. Four conjuncts, three of
them positional — far too much structure to inherit on the strength of the names matching, so
`probes/undec.c` re-derived every one of them against the **narrow** export:

| | rule |
|---|---|
| (a) | the decoration is looked for only in the **last component**, after the last backslash |
| (b) | its `]` must be the byte immediately before **the extension** — or immediately before the end of the string when there is none |
| (c) | the contents must be **decimal digits, possibly NONE**: `file[].txt` → `file.txt` |
| (d) | the `[` must **not** be the component's first byte |

Over `{a, '[', ']', '.', \, '0'}` to length 7 — 335 923 strings — the narrow export matched 174's
rule exactly, 0 mismatches, 8 166 of them actually undecorated. **That is where this would normally
have ended.** Instead `probes/space2.c` ran the same enumeration with a **space** in the alphabet,
because a missing space rule is precisely what had made changes 132, 140, 143 and 144 wrong earlier
in the same session:

```
  strings tested                          : 335923 (238267 containing a space)
  A and W disagree with EACH OTHER on     : 0
  A vs change 174's LANDED rule           : 2724 mismatches
  W vs change 174's LANDED rule           : 2724 mismatches
  A vs the amended rule (space stops ext) : 0 mismatches
  W vs the amended rule (space stops ext) : 0 mismatches
```

The smallest failing case is `". []"`: the live export undecorates it to `". "`, 174 left it alone.
**Change 174 had been wrong since it landed** — and putting the question structurally, in
`discovery/extension_space_audit2.c`, found 158, 159 and 160 carrying it as well. Eight landed
changes, one missing stopper. All eight are corrected; this one was built on the corrected rule from
the start, which is the only reason it is in this directory and not in a list of things to fix later.

### The asymmetry, which is the part that is easy to get wrong

The backslash was doing **two different jobs** in 174, and only one of them takes the space:

* it **bounds the extension search** for conjunct (b) — and a space bounds it too;
* it **delimits the component** for conjunct (d) — and a space does **not**.

So `"a b[1].txt"` → `"a b.txt"`: the `[` is not the component's first byte even though a space
precedes the name. The scan therefore tracks **two** positions, `comp` just past the last backslash
and `stop` just past the last backslash **or space**, and that is what `rbx` is pushed for. A corpus
that carries only one of the two delimiters cannot tell the two jobs apart, which is why both
`correctness.c` and the live-substitution driver enumerate **two** alphabets.

## Byte-wise is correct here, and it was screened for

`GetACP()` is **1252**, which has **zero DBCS lead bytes** (printed by `probes/bytes.c` from
`GetCPInfo`, not assumed). But the code page is a fact about the machine, not about the function, and
this repository had just abandoned `StrStrA` after it survived three weaker screens and died on the
fourth. So `probes/bytes.c` puts **all 255 non-NUL byte values at every position the rule consults**:

| position | disagreements with a byte-wise model |
|---|---|
| immediately before the `[` | 0 of 255 |
| immediately after the `]` | 0 of 255 |
| immediately after the `.` | 0 of 255 |
| as the component's first byte | 0 of 255 |
| as a lone separator before the name (dot present) | 0 of 255 |
| as a lone separator before the name (no dot) | 0 of 255 |

and it re-derives the stopper set from scratch rather than inheriting "backslash and space" from the
correction: sweeping every byte in `"x?y[1]"`, only `0x2E` blocks the removal, and in `"ab?cd[1].e"`
nothing disagrees with the corrected rule.

## Method

**ONE forward pass** yields everything the rule needs — the length, the last backslash, the last
backslash-or-space, and the last dot — from four `vpcmpeqb` per 32-byte block. Tracking the *last*
match rather than the first is why this is a forward pass with `bsr`, in the style of change 149.

The wide sibling's correction taught the loop its shape. Adding the fourth compare cost real
throughput there (1024 characters went 81.0 → 98.9 ns) until the loop was restructured around the
observation that **the overwhelmingly common block contains none of `\`, `.`, space or NUL**: one
`vpor` and one `vpmovmskb` answer that for all three at once and the three extraction blocks are
skipped entirely. That is fewer uops per block than the loop ran *before* the stopper existed. This
change was written that way from the start.

The tail move is a **descending chunk ladder** (16/8/4/2/1), not a byte loop. The tail is what this
function actually moves in practice — a decoration sits near the end of a name, so `"...[1].txt"`
moves five or six bytes and nothing goes through the 32-byte block loop at all. A byte-at-a-time
tail put a ~2.5 ns floor under every size class; at 16 characters that was most of the time spent,
and the function measured **slower there than at 64**, where the scan does strictly more work.

| | 254/dec | 254/dec+space | 16/dec | geomean |
|---|---|---|---|---|
| byte-at-a-time tail | 18.87 ns | 19.81 ns | 13.28 ns | 26.34× |
| chunk ladder | **16.22 ns** | **16.51 ns** | **12.36 ns** | **27.05×** |

## Page safety

Every 32-byte load is issued only when `(cursor & 4095) <= 4064`, proving the read stays inside the
cursor's own page — necessarily mapped, since the bytes already scanned came from it. Within 32
bytes of a page end it steps one byte and retries. The move loop issues a 32-byte block only when
the whole block lies inside the bytes that must move, so it never overreads past the terminator
either. Both are checked against a `PAGE_NOACCESS` guard page, with no decoration, with a decoration
at the very edge, and with a **space** at the very edge.

The overlapping forward move is safe for the standard reason: `dst` is strictly below `src`, so
block *i* writes `[dst+32i, dst+32i+32)` while block *i+1* reads from `src+32(i+1) >= dst+32i+32` —
no write can reach a byte not yet read. The same argument covers each rung of the ladder.

## Gate 1 — correctness: **PASS**

Three-way — our assembly vs an independent oracle vs the **live export on this PC** — comparing the
**whole buffer** every time, because the function moves a tail down and deliberately leaves the
stale bytes past the new terminator (`"file[123].txt"` → `"file.txt"` with `.txt` still behind it).
A string comparison would pass an implementation that cleared them.

- every probe-derived case explicitly, including `". []"`, `"a b[1].txt"`, `"a.b [1].c"` and
  `"a\tb[1].txt"` — a **TAB is not a stopper**, the rule is `0x20` specifically
- **exhaustive** over `{a, [, ], 1, ., \}` to length 8 — 1 727 604 strings
- **exhaustive** over `{[, ], ., 1, SPACE, z}` to length 7 — 335 923 strings, 238 267 with a space
- **exhaustive** over `{[, ], ., \, SPACE, a}` to length 7 — both delimiters at once, so the
  asymmetry above is actually exercised
- all 255 non-NUL byte values at **five** structural positions
- long paths with a space ahead of the decoration, lengths 40..300 — the vector scan must carry
  `stop` across block boundaries, which no short corpus can reach
- the decoration at many positions × 16 unaligned starts × lengths 6..120
- **400 000** randomized cases over an alphabet carrying **both a space and a tab**
- `NULL`, which the live export tolerates
- `PAGE_NOACCESS` guard sweeps, three shapes

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 / decorated | 12.36 | 63.62 | 5.15× |
| 64 / decorated | 10.31 | 216.48 | 20.99× |
| 254 / decorated | 16.22 | 766.67 | 47.25× |
| 1024 / decorated | 39.88 | 3028.12 | **75.93×** |
| 254 / no decoration | 15.83 | 764.89 | 48.31× |
| 254 / decorated + a space | 16.51 | 771.37 | 46.74× |
| 90-char real path | 12.31 | 149.00 | 12.10× |

**geomean 27.05×**, peak **81.6×** on a repeat run. Same-binary repeatability across two runs:
26.34× / 27.05× before and after the tail ladder, 26.5× / 26.3× on the same binary.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_223`. This change **pushes `rbx`** to carry the second tracked position,
so the gate is not a formality here: all 8 non-volatile GPRs and `xmm6`–`xmm15` preserved, stack
balanced (including the `NULL` early exit, which must still pop), `DF` clear.

## Live substitution — Windows ran this code

`live-substitution/live_subst_shlwapi.c`, 14-byte `jmp qword ptr [rip+0]` hot-patch of the real
export in this process's own copy-on-write copy, validate-first, sacrificial single-threaded child,
revert verified byte-for-byte:

```
[223 PathUndecorateA]  shlwapi (exhaustive x2 + space; whole buffer, stale tail incl.)
  under live patch: all match;  our-code calls = 672126
  corpus: 672126 cases over TWO exhaustive alphabets -- 476534 containing a
          SPACE (the stopper eight landed changes were missing), 17913 that
          actually removed a decoration, 280 long paths carrying a space
          AND a backslash across 32-byte block boundaries
  unpatched cleanly.
```

The wide sibling's live block, by contrast, drove 4 000 randomly built decorated paths with no space
anywhere in them and passed every session while the change was wrong. It has been rebuilt on an
exhaustive corpus as part of the correction.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Runs on Zen 3 and Zen 4 alike.
