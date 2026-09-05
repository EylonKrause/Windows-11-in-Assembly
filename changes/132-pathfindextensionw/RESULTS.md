# 132 — `shlwapi!PathFindExtensionW` — **LANDS** (5.73× geomean, up to 8.8×)

Return a pointer to the `.` introducing a path's extension, or to the terminating NUL when there is
none. shlwapi's is a scalar scan (~0.58 ns/char — 151 ns for a 254-char path).

## Contract (reverse-engineered, matched bit-exact vs live)
The extension is the last `.` that occurs after the last **backslash** — and the surprise is what does
*not* stop the search:

- **Only `\` terminates it. `/` and `:` do NOT** — even though `PathFindFileNameW` treats both as
  separators. So `"a.b/c"` returns the `.` at index 1, while `"a.b\c"` returns the terminator. Verified
  against the live export; this asymmetry between two functions in the same DLL is easy to assume away
  and was found only by fuzzing.
- A leading dot counts: `".hidden"` → index 0. A trailing dot counts: `"a.b."` → the final `.`.
- No dot (or a dot before the last `\`) → pointer to the terminating NUL.

## Method
One forward AVX2 pass. Per 32-byte block the masks for `.`, `\` and NUL are extracted, and the running
candidate is updated by the rule *"a backslash clears the candidate, a later dot sets it"* — which per
block reduces to comparing the **highest dot bit against the highest backslash bit**, with no
per-character loop. Page-safe: the first load is aligned down to 32 bytes with the leading bytes shifted
out of the masks, and every later load is 32-aligned.

Two bugs the harness caught, both worth recording because they are easy to repeat:
- **`ymm6` is non-volatile.** `xmm6`–`xmm15` must be preserved under the Win64 ABI, so using `ymm6` as a
  scratch corrupted the caller. The fix is a single volatile temp (`ymm4`) reused for all three compares.
- **`vpcmpeqw` is word-granular**, so a matching wchar sets *two* adjacent mask bits and `bsr` lands on
  the **high** byte — yielding an odd byte offset (`p+3` where `p+2` was meant). Rounding down with
  `and ecx,-2` fixes it. (`tzcnt` on the NUL mask already lands on the low byte, so it needed no change.)

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, pointer-for-pointer against the live export and an independent oracle over:
30 explicit edges; **lengths 0..200 × 16 start alignments** with `.`, `\`, `/` and `:` placed at *every*
position, plus dot-then-backslash and backslash-then-dot combinations; **600 000** fuzz strings drawn
from a path alphabet at lengths past `MAX_PATH`; and a **`VirtualAlloc` NOACCESS page-guard sweep** (the
terminator placed at the very end of a committed page, so any over-read faults).

## Benchmark — vs live `shlwapi!PathFindExtensionW`
geomean **5.73×**, every size class better:

| path | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 4.45 | 11.11 | 2.50x |
| 64 chars | 7.12 | 43.10 | 6.05x |
| 128 chars | 10.45 | 79.78 | 7.63x |
| 254 chars | 17.13 | 151.09 | **8.82x** |
| `C:\Program Files\…\wordpad.exe` | 7.34 | 44.67 | 6.08x |

## Reproduce
```
changes\132-pathfindextensionw\build.bat
```
