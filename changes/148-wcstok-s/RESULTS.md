# 148 — `ucrtbase!wcstok_s` — **LANDS** (6.14× geomean, up to 15×)

The wide half of the tokenizer pair with [147 `strtok_s`](../147-strtok-s/). ucrtbase's costs **361 ns to
pull one token out of a 254-wchar string** (~1.42 ns/char) — *slower per character than its own narrow
`strtok_s`* (0.95 ns/byte), so the wide path is the worse of the two before this change and the better
of the two after it.

## Contract (probed against the live export — standard C semantics)
Identical rules to 147:
- `str == NULL` continues from `*ctx`;
- **leading delimiters are skipped but left intact in the buffer** — only the delimiter that *ends* a
  token is overwritten with NUL. Verified live: `L",,a,,b,,"` tokenises to `"a"`, `"b"` and leaves the
  buffer as `L",,a\0,b\0,"`;
- no token left → NULL, with `*ctx` at the terminator;
- an empty delimiter set makes the whole remaining string one token.

## Method
The same two-phase AVX2 block scan as 147, at **word** granularity (`vpcmpeqw` / `vpbroadcastw`), and it
carries over the asymmetry from [135](../135-strspnw/)/[136](../136-strcspnw/): the delimiter set is
itself NUL-terminated, so it can never contain NUL — which means the terminator ends the *skip* phase
for free (it matches no delimiter), while the *token* phase must compare it explicitly and seeds its
accumulator with it.

Two wide-specific details that had to be right:
- `vpcmpeqw` sets **two** mask bits per matching character, so `tzcnt` lands on the even (low) byte of a
  pair — which is exactly the byte offset wanted. Since `wchar_t*` is always 2-byte aligned, the
  masked-prologue shift count is always even and pairs stay aligned.
- Comparisons must be word-wide, not byte-wide. The harness includes a **low-byte-collision trap** for
  precisely this: a string of `0x412C` characters (low byte `0x2C` = `','`) searched with delimiter
  `L','`. A byte-granular compare would report a false match on every character; a word-granular one
  finds none.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build. Each input is tokenised **to exhaustion**, comparing at
*every step*: the returned token offset, the context offset, and the entire buffer. Coverage: explicit
edges (leading/trailing/only delimiters, empty string, empty set, single char); **lengths 0..200 with a
delimiter at every position**, plus delimiters at both ends and two leading; sets of size 0, 1, 2, 3 and
23; **non-ASCII delimiters** (U+2100 range) and `0xFFFF`; the low-byte-collision trap above; and
**200 000** random multi-delimiter fuzz strings.

## Benchmark — vs live `ucrtbase!wcstok_s`
geomean **6.14×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars, one token | 11.63 | 31.05 | 2.67x |
| 64 chars, one token | 11.82 | 95.11 | 8.05x |
| 254 chars, one token | 24.91 | 361.44 | 14.51x |
| 1024 chars, one token | 94.23 | 1412.25 | **14.99x** |
| 254 chars, CSV (delimiter every 16) | 12.31 | 22.99 | 1.87x |

The one-token cases are the full-scan path; the CSV case returns after ~8 characters, so both sides are
dominated by call overhead. Each iteration restores the mutated buffer with a `memcpy` charged to both.

## Reproduce
```
changes\148-wcstok-s\build.bat
```
