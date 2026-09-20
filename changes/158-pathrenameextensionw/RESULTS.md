# 158 — `shlwapi!PathRenameExtensionW` — **LANDS** (3.99× geomean, up to 7.2×)

> ## CORRECTED 2026-09-15 — this change had shipped WRONG
>
> Its extension position is the one change [132 `PathFindExtensionW`](../132-pathfindextensionw/)
> derived, and **that rule was incomplete**: a **SPACE** stops the backward scan exactly as a
> backslash does, so `"a.b "` has no extension at all. 132 shipped without it because its fuzz
> alphabet contained no space; 140, 143 and 144 inherited it and were corrected in the same session.
>
> **This change was not caught by that first audit**, because that audit looked at the oracles that
> *say* they reuse 132's rule and this one does not — it open-codes its own `ref_findext`. A second,
> **structural** sweep asked the question the right way: *every* landed oracle that computes an
> extension position, whether or not it names its source. `discovery/extension_space_audit2.c`
> measured the live export against both rules over every string of `{a, '.', \, '[', ']', SPACE}`
> of length 0..7:
>
> | | mismatches |
> |---|---|
> | live export vs the rule as landed | **46 158** of 335 923 |
> | live export vs the corrected rule | **0** |
>
> The fix is one more `vpcmpeqw` against a 32-byte memory operand, OR-ed into the backslash mask — so the second stopper costs no register. It is `0x20` specifically and not
> whitespace in general — a TAB does not stop the scan.
>
> **The corpus was the real defect.** `correctness.c` now enumerates every string over
> `{a, '.', \, '/', ':', SPACE}` of length 0..7 — the previous corpora had no space in them at
> all, which is precisely why none of them could see this.

Replace a path's extension, or fail if the result would not fit in `MAX_PATH`. shlwapi's is a scalar
scan for the extension followed by a scalar copy — 76 ns for a real 90-character path, 200 ns for
254 characters.

## Contract (probed against the live export)
- `pszExt == NULL` → `FALSE`, path left completely unchanged.
- The extension is located exactly as `PathFindExtensionW` locates it, and the new one is written
  over it, terminator included.
- **The new extension is not validated**: `"obj"` (no dot) turns `f.txt` into `fobj`, `".a.b"` is
  taken whole, and `""` truncates the path at the dot.
- The result must be at most **259 characters** (`MAX_PATH - 1`). Probed exactly: a result of 259
  succeeds and 260 fails, and **on failure the destination is left completely unchanged** — not
  truncated, not emptied. That includes the no-extension case, where the insertion point is the
  terminator: a 255-character path plus `".obj"` is 259 and succeeds; 256 plus `".obj"` is 260 and
  fails.

## Method
The extension search is **change [132](../132-pathfindextensionw/) unchanged**, whose rule was
reverse-engineered and validated bit-exact over 600k fuzz cases: the extension is the last `.` after
the last **backslash**, and only `\` terminates the search — `/` and `:` do not, even though
`PathFindFileNameW` treats both as separators. Reusing it verbatim is the point of this change: the
subtle part is already proven, so what is added here is only the length arithmetic and the copy.

Per 32-byte block the `.`, `\` and NUL masks are computed together and the running candidate is
updated by "a backslash clears it, a later dot sets it", which reduces to comparing the highest dot
bit against the highest backslash bit — no per-character loop. The new extension's length is then
found with the same page-safe aligned scan, and the copy is a head/tail pair overlapping inside the
copied range.

Because the failure path must leave the buffer untouched, the length check happens **before** any
store — there is no partial write to undo.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe` compares the `BOOL` **and every byte** of a canary-filled buffer, so "unchanged on
failure" is checked as strictly as the success path. **PASS** — over path lengths **0..300 × dot
positions × extension lengths 0..8**, plus a 40-character extension long enough to push the result
over the limit from any dot position; the **259-character boundary swept exactly for result lengths
250..268**, both with a dot and with none; the `/` and `:` non-separator traps from change 132; and
**NOACCESS page-guard sweeps on both the path and the extension**.

One harness note worth recording: the path page-guard sweep starts at length 5 and always uses a
*shrinking* rename (a dot at `len-4` replaced by a 3-character extension). A shorter path has no dot,
so the insertion point is the terminator and the append runs past the end of a buffer that is exactly
`len+1` wide — which the **live export does too**, since neither implementation is told the buffer
size. The first build of this harness crashed for exactly that reason, in the test rather than the
code.

## Negative result: no extension-length limit here (2026-09-20)

Changes [159](../159-pathcchrenameextension/) and [160](../160-pathcchaddextension/), the
`kernelbase` functions that do this job, both turned out to reject an extension whose body exceeds
255 characters with `E_INVALIDARG` -- a rule neither contract recorded and neither oracle modelled.
Since 158 is the same job in `shlwapi`, it was asked the same question rather than assumed to be
different, which is how the rule was found in 160 in the first place.

[`probes/extlen.c`](probes/extlen.c) drives every extension length from 0 to 500 against a short
path, the 253..259 boundary in full, a 255-character path where the result limit is what should
decide, and a 300- and 400-character extension replacing an existing one. **0 differences.** This
export really does accept an extension of any length and is bounded only by the 259-character
result, exactly as the contract above says.

A negative result is worth the file it takes: it is the difference between "checked" and "not yet
asked", and without it the next person to notice the asymmetry has to re-derive it.

## Benchmark — vs live `shlwapi!PathRenameExtensionW`
geomean **3.99×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 12.12 | 18.27 | 1.51x |
| 64 chars | 13.63 | 48.60 | 3.57x |
| 130 chars | 20.81 | 86.90 | 4.18x |
| 254 chars | 23.66 | 171.14 | **7.23x** |
| 90-char real path | 14.52 | 61.85 | 4.26x |
| 200 chars, no extension | 19.50 | 113.88 | 5.84x |

Re-measured in full after the correction. **Both** columns came out faster than the numbers this
change originally shipped — ours *and* the system's — so the ratios moved by more than the added
compare can account for; that compare costs a fraction of a cycle per 32-byte block. The geomean is
-5% against the original measurement, which is cross-run variation of the kind this repository has
measured before (same-binary repeatability is +/-0.5%, but a rebuild relaying the code is not the
same binary). Every size class is still better, so the gate verdict is unchanged.

Each case includes restoring the path from a seed copy, paid identically by both sides, so the true
ratios for the routines alone are higher than these.

## Reproduce
```
changes\158-pathrenameextensionw\build.bat
```
