# 102 — `RtlAppendUnicodeStringToString` — **PARKED** (ntdll already optimal)

Unlike its NUL-terminated cousin [101](../101-rtlappendunicodetostring/), the *counted* append
takes `src->Length` directly — so there is **no `wcslen` call to eliminate**, and the only
work is a bounds check plus the copy, which ntdll already does efficiently. Our frameless
inline SSE copy ties at small sizes and **loses at 128 wchars** (0.71×: ntdll's block copy of
256 bytes beats a 16-byte `movdqu` loop). geomean **1.02×** → a size class regresses, so it
does not land.

Correctness is bit-exact vs the live export (NTSTATUS + `Length` + buffer) over init 0..200 ×
add 0..140 × MaximumLength boundaries (including the `src->Length == 0` no-op and the
`MaximumLength - Length >= 2` NUL rule), so the impl is kept as a correct reference; the
function itself belongs on the KEEP-AS-IS list.

| append (wchars) | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 2 | 3.79 | 5.80 | 1.53x |
| 16 | 4.90 | 4.71 | 0.96x |
| 32 | 5.80 | 6.06 | 1.05x |
| 128 | 11.15 | 7.93 | 0.71x |

## Reproduce
```
changes\102-rtlappendunicodestringtostring\build.bat
```
