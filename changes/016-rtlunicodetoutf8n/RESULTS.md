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
