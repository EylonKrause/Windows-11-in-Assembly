# 159 — `kernelbase!PathCchRenameExtension` — **LANDS** (3.94× geomean, up to 5.7×)

The `PathCch` counterpart of [158](../158-pathrenameextensionw/), and a much stricter function: 87 ns
for a real 90-character path, 213 ns for 254 characters.

## Contract (probed exhaustively against the live export)
Unlike its shlwapi cousin this one validates everything, and it has **two different failure modes**
that a careless reading would merge:

| input | result |
|---|---|
| `pszPath == NULL`, `pszExt == NULL`, `cch == 0`, or `cch > 32768` | `E_INVALIDARG`, buffer untouched |
| path not NUL-terminated within `cch` | `E_INVALIDARG`, buffer untouched |
| path length > 259 | `E_INVALIDARG` — the limit is on the **input** length |
| extension contains a space, backslash, or non-leading dot | `E_INVALIDARG`, buffer untouched |
| result does not fit in `cch` | **`STRSAFE_E_INSUFFICIENT_BUFFER` (0x8007007A) with a PARTIAL WRITE** |
| otherwise | `S_OK` |

Three of these were only settled by measurement:

- **The 259 limit is on the input, not the result.** A 260-character path fails even when the new
  extension would shorten it to 252. (An earlier length sweep of mine appeared to show *every* length
  failing — that was my own bug: I had passed `cch = 40000`, which exceeds `PATHCCH_MAX_CCH` and
  fails first.)
- **The rejected character set is exactly three.** A full 65536-character sweep, at both the first
  and a later position, says only space (`U+0020`), backslash (`U+005C`) and a non-leading dot are
  refused. `/`, `:`, `*`, `?`, `"`, `<`, `>` and `|` are all accepted, so `".a/b"` succeeds.
- **The insufficient-buffer path writes to the caller's buffer.** It leaves exactly `cch-1`
  characters of the result plus a terminator — the opposite of 158, which leaves the buffer untouched
  on failure. Probed: a 6-character path with `".obj"` gives `"C:\a\f."` at `cch = 8`, `"C:\a\f.o"`
  at 9, and succeeds at 11.

A leading dot on the extension is optional (`"obj"` and `".obj"` are equivalent), and both `""` and
`"."` mean "remove the extension" — a lone dot does **not** leave a trailing one.

## Method
Three bounded 32-byte AVX2 passes with aligned loads, so none can cross into a page the caller did not
give us: the path's length (bounded by `cch`), the extension's length **fused with its validity
check** — the `.`, `\` and ` ` masks are OR-ed and then masked down to the bytes before the
terminator, so one pass answers both "how long" and "is it legal" — and the extension position, using
the block scan from [132](../132-pathfindextensionw/) whose rule is already validated bit-exact.
Probing confirms this export agrees with that rule: `"a.b/c"` renames to `"a.obj"`.

## A bug the fixed-alignment grid did not catch
The first working build set the position base of the extension-position scan to the **aligned** block
address rather than the string pointer. The masks are shifted to be relative to the string, so that
is correct only when the path happens to be 32-aligned — which every path in the grid was, because
`chk` copied each one to offset 0 of its buffers. The grid passed; only the page-guard sweep, where
the alignment moves with the length, failed, and it failed with matching `HRESULT`s and differing
buffers, which is what pointed at the write rather than the validation.

The grid now sweeps **16 path alignments**, so the class of bug cannot hide there again.

## Correctness — bit-exact vs live kernelbase + oracle
`correctness.exe` compares the `HRESULT` **and every byte** of a canary-filled buffer, which is what
separates the two failure modes. **PASS** — all five argument-validation paths; every rejected
character at six positions with and without a leading dot, plus 14 characters that must be *accepted*;
**16 path alignments × path lengths 0..40 × dot positions × extension lengths 0..6 × every `cch` from
1 to `plen+12`**; the 259 input-length limit swept exactly; and NOACCESS page-guard sweeps on both the
path and the extension, the latter including a *rejected* extension so the bad-character scan is
exercised at a page edge too.

## Benchmark — vs live `kernelbase!PathCchRenameExtension`
geomean **3.94×**, every size class better:

| case | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 16 chars | 16.95 | 28.91 | 1.71x |
| 64 chars | 19.50 | 73.13 | 3.75x |
| 130 chars | 27.79 | 122.75 | 4.42x |
| 254 chars | 37.37 | 212.62 | **5.69x** |
| 90-char real path | 20.26 | 86.70 | 4.28x |
| 200 chars, no extension | 31.49 | 171.17 | 5.44x |

Each case includes restoring the path from a seed copy, paid identically by both sides.

## Reproduce
```
changes\159-pathcchrenameextension\build.bat
```
