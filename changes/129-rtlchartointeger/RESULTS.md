# 129 — `ntdll!RtlCharToInteger` — **PARKED** (bit-exact; wins 1.88× on the base-0 hex path, loses on short inputs)

The parse-side complement of the landed [097 `RtlIntegerToChar`](../097-rtlintegertochar/). The contract
was fully reverse-engineered and the implementation is **bit-exact vs the live export**, but ntdll's
routine is already efficient for short inputs, so a size class regresses and it cannot LAND.

## Contract (reverse-engineered, matched bit-exact: NTSTATUS **and** `*Value`)
Several genuinely surprising rules, all found by fuzzing rather than assumed:
- **Leading skip uses a SIGNED char compare** — `while ((signed char)*s <= ' ')` — so it skips not only
  `0x01`–`0x20` but also **`0x80`–`0xFF`** (high bytes are negative). Verified byte-by-byte over all 255.
- Whitespace is skipped only **before** the sign: `"- 42"` → 0. Exactly one `+`/`-` is consumed.
- `Base == 0` auto-detects `0x`/`0b`/`0o` — **lowercase only**, so `"0X10"` parses as decimal `0` — and a
  bare leading `0` means **decimal, not octal**: `"0777"` → 777.
- `Base` outside `{0,2,8,10,16}` → `STATUS_INVALID_PARAMETER` with **`*Value` left untouched**
  (`"z"` base 36 → error, sentinel preserved).
- Digits accumulate **mod 2^32 with no overflow detection**: `"4294967296"` → 0.
- No digits is still `STATUS_SUCCESS` with value 0 (`""`, `"abc"`).

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. NTSTATUS and `*Value` match the live export and an independent oracle over
explicit edges, **every one of the 256 byte values** in leading position, embedded position and before a
`0x` prefix, across 5 bases, plus a 16-value base sweep (including invalid bases) and **3 000 000** fuzz
strings.

## Benchmark — why it parks
| input | ours ns | ntdll ns | ratio |
|---|---|---|---|
| `"1234567890"` base 10 | 8.67 | 8.79 | 1.01x |
| `"0xDEADBEEF"` base 0 | 8.89 | 16.67 | **1.88x** |
| `"42"` base 10 | 5.78 | 4.67 | 0.81x |
| `"  -2147483648"` base 0 | 9.78 | 9.36 | 0.96x |

geomean 1.10× → **PARKED**. The genuine win is the **base-0 prefix path**, where ntdll costs ~16.7 ns
(nearly double its own decimal path) and the hand version does it in 8.9 ns. On a 2-character parse the
whole call is fixed overhead and ntdll is ~5 cycles leaner.

Two optimisation attempts were measured and **rejected**, both recorded rather than hidden:
- replacing the 256-entry digit table with branch-light arithmetic (no second dependent load) — no
  measurable change; the gap is call overhead, not the digit path;
- per-base specialised loops for 10 and 16 (`×10` via two `lea`s, `×16` via `shl`) — **slower**
  (geomean 1.05×): the dispatch branches cost more than the shortened multiply chain saves.

## Why kept
Recorded honestly alongside the other already-optimized-incumbent parks (`memcmp`, `crc32`, `strstr`,
[125](../125-rtlfindclearbits/)). The reverse-engineered contract above is the durable value — in
particular the signed-char skip and the lowercase-only/decimal-leading-zero base rules, which differ
from every CRT `strtol` in this repo ([110](../110-strtol/)).

## Reproduce
```
changes\129-rtlchartointeger\build.bat
```
