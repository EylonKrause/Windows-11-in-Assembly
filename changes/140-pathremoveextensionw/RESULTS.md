# 140 — `shlwapi!PathRemoveExtensionW` — **LANDS** (2.98× geomean, up to 8.3×)

Truncate a path at its extension, in place. shlwapi's is a scalar scan (186 ns for a 254-char path).

## Contract (matched bit-exact vs live)
This is [132 `PathFindExtensionW`](../132-pathfindextensionw/) plus a single store — the extension
position is found by the same validated rule (the last `.` after the last **backslash**, with `/` and
`:` *not* stopping the search), and a NUL is written there. When there is no extension that position is
already the terminator, so the store is harmless and needs no branch. Confirmed including 132's quirks:
`"a.b/c"` → `"a"` (a slash does not protect the dot) while `"a.b\c"` is unchanged.

**But it has one rule 132 does not: a MAX_PATH guard.** Once the string reaches **260 characters the
function does nothing at all**, regardless of where the dot sits. Measured at the boundary: length 259
truncates, length 260 does not. `PathFindExtensionW` has no such limit (change 132 fuzzed cleanly to
length 400), so the two functions genuinely disagree on long paths — this was found by fuzzing, after an
initial implementation that simply reused 132 failed on exactly the over-length cases.

## Method
132's single-pass AVX2 scan (per 32-byte block: masks for `.`, `\` and NUL; the candidate updated by
"a backslash clears it, a later dot sets it"), then the length is taken from the terminator the scan
already found — so the MAX_PATH guard costs one compare and no extra pass.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, comparing the **whole buffer** (in-place function) against the live export
and an independent oracle over: 20 explicit edges; **lengths 0..200 × 8 alignments** with `.`, `\`, `/`
and `:` at every position plus both dot/backslash orders; the **259/260 MAX_PATH boundary** swept from
250 to 300 with the dot at six different offsets from the end; and **400 000** path-alphabet fuzz strings
at lengths up to 300 (which is what exposed the guard in the first place).

## Benchmark — vs live `shlwapi!PathRemoveExtensionW`
geomean **2.98×**, every size class better:

| path | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 12.62 | 16.44 | 1.30x |
| 64 chars | 13.06 | 50.81 | 3.89x |
| 254 chars | 22.48 | 185.92 | **8.27x** |
| 1024 chars (over MAX_PATH — both bail) | 90.74 | 172.09 | 1.90x |
| `C:\Program Files\…\wordpad.exe` | 84.17 | 248.00 | 2.95x |

The 1024-char row is the guard case: both implementations must still measure the length before deciding
to do nothing. Each iteration also restores the mutated buffer with a `memcpy` charged to both sides.

## Reproduce
```
changes\140-pathremoveextensionw\build.bat
```
