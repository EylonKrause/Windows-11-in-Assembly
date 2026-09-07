# 158 — `shlwapi!PathRenameExtensionW` — **LANDS** (4.21× geomean, up to 6.5×)

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

## Benchmark — vs live `shlwapi!PathRenameExtensionW`
geomean **4.21×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 15.21 | 24.68 | 1.62x |
| 64 chars | 17.60 | 64.22 | 3.65x |
| 130 chars | 22.80 | 112.23 | 4.92x |
| 254 chars | 30.92 | 200.20 | **6.47x** |
| 90-char real path | 17.14 | 76.03 | 4.44x |
| 200 chars, no extension | 24.30 | 161.32 | 6.64x |

Each case includes restoring the path from a seed copy, paid identically by both sides, so the true
ratios for the routines alone are higher than these.

## Reproduce
```
changes\158-pathrenameextensionw\build.bat
```
