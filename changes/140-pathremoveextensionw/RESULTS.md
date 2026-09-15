# 140 — `shlwapi!PathRemoveExtensionW` — **LANDS** (2.98× geomean, up to 8.3×)

> ## CORRECTED 2026-09-15 — this change had shipped WRONG
>
> It reused change 132's extension rule, and that rule was **incomplete**: a **SPACE** stops the
> backward scan exactly as a backslash does, so `"a.b "` has no extension at all. 132 shipped without
> it because its fuzz alphabet contained no space, and this change inherited the gap — its own oracle
> says so in as many words ("exactly the pointer `PathFindExtensionW` returns").
>
> `discovery/extension_space_audit.c` measured the damage over every string of
> `{a, '.', \, space}` of length 0..9:
>
> | | mismatches |
> |---|---|
> | live export vs the OLD rule (backslash only) | **57 746** of 349 525 |
> | live export vs the corrected rule | **0** |
>
> The fix is one extra compare per block against a 32-byte memory operand, so it costs no register.
> It is `0x20` specifically and not whitespace in general: `"a.b\t"` still has an extension.
>
> **The corpus was the real defect.** `correctness.c` now **enumerates** all 87 381 strings over
> `{a, '.', \, space}` of length 0..8 instead of sampling them, and its fuzz alphabet contains a
> space and a tab. Verified protective: reverting the implementation makes it fail immediately on
> `". "`. The change was also added to the ABI gate, since the amendment introduced a second vector
> temp and a callee-saved one would have been invisible to correctness.

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
