# 021 — `RtlUnicodeToMultiByteN` (AVX2) — **LANDED** (core ntdll, 3.93× geomean)

The raw counted UTF-16 → ANSI converter (what `RtlUnicodeStringToAnsiString` calls internally), invoked
directly by many callers. 1 byte per wchar (SBCS codepage).

- **Contract:** `NTSTATUS RtlUnicodeToMultiByteN(char* dst, ULONG maxBytes, PULONG outLen,
  const wchar_t* src, ULONG srcBytes)`. Writes up to `maxBytes`; `*outLen` = bytes written; always returns
  `STATUS_SUCCESS` (it does not report overflow — the caller compares `outLen`).
- **Compared against:** live `ntdll.dll!RtlUnicodeToMultiByteN`. **ISA:** AVX2.
- All-ASCII blocks pack 16 → 16 (and 8 → 8) bytes; other wchars via `wia_ansimap[]` from the OS codepage.

## Correctness — PASS

Output bytes + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + non-ASCII) and truncating-`maxBytes` cases.

## Speed — LANDS (no size class regressed)

| length | ours ns | ntdll ns | ratio |
|---:|---:|---:|---:|
| 8 | 3.35 | 5.13 | 1.53× |
| 128 | 8.47 | 40.77 | 4.81× |
| 4096 | 197.31 | 1108.40 | 5.62× |
| 32000 | 1519 | 8780 | 5.78× |

**Overall geomean 3.93× faster. No size class regressed → LANDS.**

## Iteration (the "don't give up" fix)

The raw N-converter's ntdll path is leaner than the STRING wrapper, so 8-wchar strings first regressed
(0.82×). Adding an 8-wide ASCII pack (in addition to the 16-wide) made 8/cs 1.53×.

## Reproduce
```
changes\021-rtlunicodetomultibyten\build.bat
```
