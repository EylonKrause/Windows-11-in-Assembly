# 147 — `ucrtbase!strtok_s` — **LANDS** (5.43× geomean, up to 21.9×)

Pull the next token out of a string. ucrtbase's costs **240 ns for a 254-byte string** (~0.93 ns/byte)
because it is two scalar set-scans back to back — `strspn` to skip leading delimiters, then `strcspn` to
find the token end. Each becomes one AVX2 block scan.

## Contract (probed against the live export — standard C semantics)
- `str == NULL` continues from `*ctx`.
- **Leading delimiters are skipped but left intact in the buffer** — only the delimiter that *ends* a
  token is overwritten with NUL. `",,a,,b,,"` tokenises to `"a"`, `"b"` and leaves the buffer as
  `",,a\0,b\0,"`. That asymmetry is the easy thing to get wrong, so the harness compares the whole
  buffer at every step rather than just the returned tokens.
- No token left → NULL, with `*ctx` pointing at the terminator.
- An empty delimiter set makes the whole remaining string one token.

## Method
Both phases use the block scan from [135](../135-strspnw/)/[136](../136-strcspnw/), byte-granular: for
each 32-byte block every delimiter is broadcast and compared and the results OR-ed.

The two phases differ in exactly one way, for the same reason as 135 vs 136: the delimiter set is itself
NUL-terminated, so it can never *contain* NUL. In the skip phase that means the terminator ends the scan
for free (it matches no delimiter); in the token phase the span continues *while* bytes are outside the
set, so the terminator must be compared explicitly and the accumulator is seeded with it.

Page-safe: loads are 32-byte aligned with the leading bytes shifted out of the mask.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build. Each input is tokenised **to exhaustion**, comparing at
*every step*: the returned token offset, the context offset, and the entire buffer. Coverage: explicit
edges (leading/trailing/only delimiters, empty string, empty set, single char); **lengths 0..200 with a
delimiter at every position**, plus delimiters at both ends and two leading; sets of size 0, 1, 2, 3 and
23 (all characters are delimiters); and **200 000** random multi-delimiter fuzz strings.

## Benchmark — vs live `ucrtbase!strtok_s`
geomean **5.43×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 B, one token | 15.34 | 20.69 | 1.35x |
| 64 B, one token | 11.79 | 64.69 | 5.49x |
| 254 B, one token | 14.90 | 240.45 | 16.14x |
| 1 KB, one token | 42.41 | 928.26 | **21.89x** |
| 254 B, CSV (delimiter every 16 B) | 9.11 | 16.46 | 1.81x |

The one-token cases are the full-scan path; the CSV case returns after ~8 bytes, so both are dominated
by call overhead. Each iteration restores the mutated buffer with a `memcpy` charged to both sides.

## Reproduce
```
changes\147-strtok-s\build.bat
```
