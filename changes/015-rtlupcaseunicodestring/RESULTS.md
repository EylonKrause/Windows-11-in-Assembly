# 015 — `RtlUpcaseUnicodeString` (AVX2) — **LANDED** (core ntdll, 9.12× geomean)

Upcases a whole UTF-16 string into a destination buffer — a *transform*, not a comparison. Used all over
the OS to normalize names before hashing/comparison. The biggest win in the repo so far.

- **Contract (no-allocate path):** `NTSTATUS RtlUpcaseUnicodeString(UNICODE_STRING* dst,
  const UNICODE_STRING* src, BOOLEAN AllocateDestinationString)` with `Allocate == FALSE` — writes upcased
  `src` into the caller's `dst->Buffer`, sets `dst->Length`, returns `STATUS_SUCCESS` (or
  `STATUS_BUFFER_OVERFLOW` if `dst->MaximumLength < src->Length`). `Allocate == TRUE` (heap allocation) is
  out of scope and returns `STATUS_INVALID_PARAMETER`.
- **Compared against:** live `ntdll.dll!RtlUpcaseUnicodeString`. **ISA:** AVX2.

## Correctness — PASS

Output buffer, `Length`, and status all bit-exact vs a scalar reference and live `ntdll` across `n=0..280`,
ASCII + non-ASCII (to U+05FF), plus the buffer-overflow case. All-ASCII 16-wchar blocks upcase in-register
(a–z range subtract); blocks with any wchar `>= 0x80` use `wia_upcase[]` (built from the OS), so folding is
bit-exact for all of Unicode.

## Speed — LANDS (no size class regressed)

Min-of-200, no-allocate (caller buffer).

| length (wchars) | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 8 | 4.23 | 9.04 | 2.14× | BETTER |
| 32 | 4.01 | 27.31 | 6.80× | BETTER |
| 128 | 7.58 | 104.96 | 13.84× | BETTER |
| 512 | 31.19 | 488.76 | **15.67×** | BETTER |
| 4096 | 208.10 | 3221.88 | 15.48× | BETTER |
| 32000 | 2153 | 25394 | 11.79× | BETTER |

**Overall geomean 9.12× faster. No size class regressed → LANDS.** ntdll upcases one character at a time
(~2.5 GB/s); the vectorized transform reaches ~35 GB/s (≈ 14× on medium strings).

## Reproduce
```
changes\015-rtlupcaseunicodestring\build.bat
```
