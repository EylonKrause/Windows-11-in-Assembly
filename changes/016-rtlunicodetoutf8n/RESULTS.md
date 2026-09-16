# 016 — `RtlUnicodeToUTF8N` (AVX2) — **LANDED** (core ntdll, 2.82× geomean)

The pending hard one: UTF-16 → UTF-8 conversion. Used across the OS wherever names cross the UTF-8 boundary
(modern path/registry/console interop). A real UTF-8 encoder — 1/2/3/4-byte sequences, surrogate-pair
decode, lone-surrogate handling, and exact buffer-overflow semantics — with an AVX2 ASCII fast path.

- **Contract:** `NTSTATUS RtlUnicodeToUTF8N(void* dst, ULONG dstMax, PULONG outLen, const wchar_t* src,
  ULONG srcBytes)`.
- **Compared against:** live `ntdll.dll!RtlUnicodeToUTF8N`. **ISA:** AVX2.

## Exact behavior (nailed against the oracle first)

A scalar model was validated vs live ntdll (0 mismatches / 60,000) before porting:
- `< 0x80` → 1 byte; `< 0x800` → 2 bytes; non-surrogate `< 0x10000` → 3 bytes.
- High + low surrogate → decode to a 4-byte sequence.
- Any **lone** surrogate → `U+FFFD` (`EF BF BD`) and status `STATUS_SOME_NOT_MAPPED` (0x00000107).
- **Overflow:** complete sequences are written while they fit; the first sequence that does not fit stops
  the conversion, `*outLen` = bytes actually written, status `STATUS_BUFFER_TOO_SMALL` (0xC0000023).

## ASCII fast path

When 16 consecutive wchars are all `< 0x80` and there is room, they pack 16→16 bytes in one shot
(`vpackuswb` + a `vpermq 0xD8` lane fixup); an 8-wide path handles 8–15; everything else is exact scalar.

## The measuring mode — added 2026-09-16, because it was missing

`RtlUnicodeToUTF8N(NULL, 0, &produced, src, srcLen)` is the documented way to ask this function how
many bytes the output will need: the shipped export returns `STATUS_SUCCESS` with `produced` set and
writes nothing. This implementation returned `STATUS_BUFFER_TOO_SMALL` with `produced = 0`, and with
a NULL pointer and a **non-zero** size it dereferenced the pointer and faulted.
`discovery/utf8n_null_destination.c` is the evidence, committed separately in `8bfd5db`; the fix is
commit `417f31f`.

**Why the gates did not catch it.** This change is bit-exact against the live export at every real
capacity from 0 upward — and every case in those corpora passes a *real* destination buffer. A NULL
destination is not an edge of the LENGTH, which is what the corpora sweep; it is a different **mode**
of the same function, and nothing asked for it.

**The fix** is a counting pass taken when the destination is NULL, using the same rule the encoder
already implements: below `0x80` is one byte, below `0x800` is two, a high surrogate followed by a
low one is four and consumes both, and everything else — including a lone surrogate, which becomes
`U+FFFD` — is three, with a lone surrogate also setting `STATUS_SOME_NOT_MAPPED`. That rule was
verified against the live measuring mode over 200,000 random strings, at 0 disagreements on the size
and on the status, before a line of assembly was written.

`measuring.c` gates it: **122,006 cases, 0 mismatches** — every length from 0 to 400 for ASCII,
two-byte, three-byte, surrogate-pair and lone-surrogate inputs, the NULL-with-non-zero-size case that
used to fault, and 120,000 randomised. It also checks that the measured size is exactly the size a
real conversion produces. The live measuring mode answered `SUCCESS` 64,897 times and
`SOME_NOT_MAPPED` 57,108, so both statuses are exercised.

**What it costs, measured rather than waved away.** The two-instruction test at the entry costs
**0.12 ns** on the 8-byte row — 2.93 ns before, 3.05 after — moving the geomean on this machine from
2.68× to 2.62×. Moving the test *after* the prologue, so that it might issue alongside the seven
pushes, was tried and measured **worse** at 3.13 ns; the entry is the better of the two placements.
Every size class still beats the shipped code and the change still **LANDS**.

## Correctness — PASS

Output bytes, `outLen`, and status all bit-exact vs a scalar reference and live `ntdll` across 80,000 fuzz
cases mixing ASCII / 2-byte / 3-byte / surrogate content, plus small-buffer overflow cases.

## Speed — LANDS (no size class regressed)

Min-of-200, ASCII input.

| length (wchars) | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 8 | 4.01 | 5.79 | 1.44× | BETTER |
| 128 | 8.89 | 25.62 | 2.88× | BETTER |
| 512 | 24.75 | 93.78 | 3.79× | BETTER |
| 32000 | 1355 | 5355 | 3.95× | BETTER |

**Overall geomean 2.82× faster. No size class regressed → LANDS.**

## Iteration (the "don't give up" widening)

The first ASCII path packed 8 wchars → 8 bytes (128-bit) and landed at 1.83×. Widening to 16 wchars → 16
bytes (256-bit `vpackuswb` + `vpermq` lane fixup) raised it to **2.82× (up to 3.95×)**.

## Reproduce
```
changes\016-rtlunicodetoutf8n\build.bat
```
and for the measuring mode:
```
ml64 /c impl.asm & cl /O2 measuring.c impl.obj /Fe:measuring.exe & measuring.exe
```
