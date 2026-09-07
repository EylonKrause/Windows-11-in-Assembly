# 156 — `ucrtbase!wcsspn` — **LANDS** (9.10× geomean, up to 29.9×)

The naive $O(nm)$ scalar loop: 404 ns for a 254-character string against a 3-character set, about 7
cycles per character. Its **narrow** sibling `strspn` does the same character count in 178 ns, because
with 256 possible byte values it can afford a bitmap of the set — a wide character has 65536, so that
trick does not transfer and the wide half was left scalar. The same split as
[148](../148-wcstok-s/) and [149](../149-wcsrchr/).

## Contract
A probe confirmed that `ucrtbase!wcsspn` and `shlwapi!StrSpnW` (change [135](../135-strspnw/)) agree on
**every** edge case that could separate them — empty set, empty string, both empty, no match, and a
set member with a zero low byte — so the core of 135 satisfies this contract unchanged. Only the
return type differs (`size_t` vs `int`), and both exits already leave a zero-extended value in `rax`.

## Method
Sixteen characters per step: for each 32-byte block every set character is compared and the results
OR-ed into an "in set" mask; the first character **not** in the set ends the span. The terminator
needs no special case — a set is itself NUL-terminated, so it can never contain NUL, and the NUL
therefore fails every compare and stops the span naturally. (That is the one asymmetry with the
complement span in [157](../157-wcscspn/), which must compare the terminator explicitly.)

### The set is hoisted out of the block loop
Change 135 re-walked the set *inside* every 32-byte block, broadcasting each member afresh — about
seven instructions per member per block. Here the first three members are broadcast **once**, before
the loop, into `ymm2`/`ymm4`/`ymm5`, leaving the block body straight-line.

When the set is shorter than three, the spare registers take a **duplicate of member 0**. That is
free of consequence because the compare results are OR-ed and $a \lor a = a$, and it removes the need
for three separate specialised loops. An empty set is answered up front (nothing is in it, so the
span is 0), which is also what keeps the duplicate trick well defined — member 0 is guaranteed to
exist past that point. Sets longer than three still walk the remainder from memory, a tail that costs
two µops per block when it is empty.

Only `ymm0`–`ymm5` are usable (xmm6–xmm15 are non-volatile under Win64), which is exactly enough for
data, accumulator, three members and one scratch.

Besides the raw instruction count, the hoist removed a real measurement hazard: see
[157](../157-wcscspn/) for the case where three added µops moved a result between 96 and 125 ns with
no change to the work done.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** — **16 alignments × lengths 0..200 × set sizes 0..8** (with the string
drawn from an 8-letter alphabet so the span ends somewhere different for each set), plus the full
alphabet at every length; a **single non-member planted at every position of every length**, which is
what pins the exact stopping point; **zero-low-byte (`0x4100`), zero-high-byte (`0x0041`) and
`0xFFFF`** wchar traps that a byte-granular compare would fail; and **NOACCESS page-guard sweeps on
both the string and the set** — the set matters because it is walked scalar-wise and must stop at its
own NUL.

## Benchmark — vs live `ucrtbase!wcsspn`
geomean **9.10×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars, 3-set | 3.56 | 28.03 | 7.88x |
| 64 chars, 3-set | 6.23 | 102.77 | 16.50x |
| 254 chars, 3-set | 14.90 | 404.25 | 27.13x |
| 1024 chars, 3-set | 53.60 | 1603.48 | **29.91x** |
| 254 chars, 1-set (stops at 1) | 3.78 | 6.01 | 1.59x |
| 254 chars, 16-set | 119.00 | 404.18 | 3.40x |

The 16-member set is the weakest case, and honestly so: thirteen of those members fall past the three
hoisted registers and are re-walked from memory on every block. It is still 3.4×.

## Reproduce
```
changes\156-wcsspn\build.bat
```
