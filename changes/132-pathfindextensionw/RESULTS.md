# 132 — `shlwapi!PathFindExtensionW` — **LANDS** (6.26× geomean, up to 8.9×)

> ## CORRECTED 2026-09-15 — this change had shipped WRONG
>
> The rule below originally had only the backslash in it, and this change was described as
> "reverse-engineered and validated bit-exact vs the live export over 600k fuzz". **It was not.** It
> disagreed with the live `PathFindExtensionW` on **295 513 of 2 015 539** enumerated strings.
>
> **A SPACE stops the backward scan exactly as a backslash does.** `"a.b "` returns the terminator,
> not the dot. The oracle, the implementation and the correctness test were all wrong *together*,
> because the fuzz alphabet was `{a, b, '.', '\', '/', ':', '.', 'c'}` — **no space** — so the corpus
> could not produce the failing shape. A test that shares its blind spot with the thing it tests
> proves nothing.
>
> It was caught while probing the narrow sibling for change 217, whose probe enumerated
> `{a, '.', '\', '/', ':'}` exhaustively (0 mismatches against the old rule), then widened the
> alphabet by two characters and got 118 587. The smallest failing case is `". "`.
>
> The amendment was verified rather than guessed, over two alphabets and 2 015 539 strings each:
>
> | | mismatches |
> |---|---|
> | live `PathFindExtensionW` vs the OLD rule | **295 513** |
> | live `PathFindExtensionW` vs the corrected rule | **0** |
> | live `PathFindExtensionA` vs the corrected rule | **0** |
>
> `correctness.c` no longer samples this alphabet, it **enumerates** it: all 335 923 strings over
> `{a, '.', '\', '/', ':', space}` of length 0..7, which would have failed loudly on day one. The
> change is now also driven live (it never had been) — see below.

Return a pointer to the `.` introducing a path's extension, or to the terminating NUL when there is
none. shlwapi's is a scalar scan (~0.58 ns/char — 151 ns for a 254-char path).

## Contract (corrected; verified against the live export by enumeration)
The extension is the last `.` that occurs after the last **stopper**, where a stopper is a
**backslash or a space** — and the surprise is what does *not* stop the search:

- **`\` and a SPACE terminate it. `/` and `:` do NOT** — even though `PathFindFileNameW` treats both
  as separators. So `"a.b/c"` returns the `.` at index 1, while `"a.b\c"` and `"a.b "` both return
  the terminator.
- It is **0x20 specifically, not whitespace in general**: `"a.b\t"` still returns the dot. Of 255 byte
  values placed after a dot, exactly three stop it counting — `0x20`, `0x2E` and `0x5C` — and the
  latter two are already explained by the last-dot and backslash rules.
- A leading dot counts: `".hidden"` → index 0. A trailing dot counts: `"a.b."` → the final `.`.
- No dot (or a dot before the last stopper) → pointer to the terminating NUL.

## Method
One forward AVX2 pass. Per 32-byte block the masks for `.`, the **stoppers** and NUL are extracted,
and the running candidate is updated by the rule *"a stopper clears the candidate, a later dot sets
it"* — which per block reduces to comparing the **highest dot bit against the highest stopper bit**,
with no
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

## Live substitution — PASS (added with the correction)

This change had never been driven live. It is now, **together with its narrow sibling 217 and against
an exhaustive corpus**, because it is precisely the change that shipped wrong on shapes a random
corpus could not reach: 55 987 strings over `{a, '.', '\', '/', ':', space}` of length 0..6, of which
**36 456 contain a space**. Both exports patched, identical results, both prologues restored
byte-for-byte.
