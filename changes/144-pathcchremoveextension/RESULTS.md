# 144 — `kernelbase!PathCchRemoveExtension` — **LANDS** (2.59× geomean, up to 5.9×)

Truncate a path at its extension, in place. kernelbase's is scalar (157 ns for a 254-char path).

## Contract (probed against the live export)
Same extension rule as [132](../132-pathfindextensionw/)/[143](../143-pathcchfindextension/) — the last
`.` after the last **backslash**, with `/` and `:` not stopping the search — plus three return/validation
rules, two of which are easy to get wrong:

- **S_OK when an extension was removed, `S_FALSE` (1) when there was none.** Not a bool, and not S_OK in
  both cases; the buffer is left alone in the S_FALSE case.
- `cchPath` must be in **[1, 32768]** and the string must terminate strictly inside it — as in 143.
- **The string LENGTH must additionally be ≤ 259**, *independent of `cchPath`*. Measured: length 259
  succeeds and length 260 fails **even with `cch` = 1000**, while `PathCchFindExtension` accepts both.
  Two adjacent functions in the same family with different limits — found by fuzzing, after an
  implementation that simply reused 143's validation failed on exactly the over-length cases.

That makes four different answers to "what happens past MAX_PATH" among the path functions measured
here: [140](../140-pathremoveextensionw/) silently does nothing, [141](../141-pathremoveblanksw/) has no
limit, [142](../142-pathaddbackslashw/) returns NULL, and this one returns `E_INVALIDARG`.

## Method
Change 143's bounded AVX2 scan (132's candidate rule with every mask clipped to `cchPath`), then the
length is taken from the terminator the scan already found — so the extra length limit costs one compare
and no additional pass — and a NUL is written at the extension position when there is one.

## Correctness — bit-exact vs live kernelbase + oracle
`correctness.exe`: **PASS**, comparing the **HRESULT (including S_FALSE) and the whole buffer** — the
in-place no-op cases must leave it byte-identical — over: 20 explicit edges; `cch` limits 0 / len / len+1
/ 32768 / 32769 / 100000; unterminated buffers; **lengths 0..200 × 8 alignments** with `.`, `\`, `/` and
`:` at every position; and **300 000** path-alphabet fuzz strings up to length 400 alternating exact and
generous `cch` (which is what exposed the length limit).

## Benchmark — vs live `kernelbase!PathCchRemoveExtension`
geomean **2.59×**, every size class better:

| path | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 16 chars | 12.01 | 15.75 | 1.31x |
| 64 chars | 13.47 | 48.68 | 3.61x |
| 254 chars | 26.70 | 156.66 | **5.87x** |
| 1024 chars (over the length limit — both reject) | 101.46 | 169.36 | 1.67x |
| `C:\Program Files\…\wordpad.exe` | 13.25 | 33.53 | 2.53x |

The 1024-char row is the rejection path: both implementations must still measure the length before
returning `E_INVALIDARG`. Each iteration restores the mutated buffer with a `memcpy` charged to both.

## Reproduce
```
changes\144-pathcchremoveextension\build.bat
```
