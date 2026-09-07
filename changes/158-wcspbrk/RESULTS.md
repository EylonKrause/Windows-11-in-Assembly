# 158 — `ucrtbase!wcspbrk` — **LANDS** (9.08× geomean, up to 22.1×)

Pointer to the first character of the string that appears in the set, or NULL. The live one is the
naive $O(nm)$ scalar loop — 347 ns for 254 characters against a 3-character set, against 130 ns for
the narrow `strpbrk`, which can bitmap its 256 possible values.

## Contract
A probe confirmed that `ucrtbase!wcspbrk` and `shlwapi!StrPBrkW` (change [137](../137-strpbrkw/))
agree on every distinguishing edge case — empty set → NULL, empty string → NULL, no match → NULL, and
a set member with a zero low byte.

## Method
The same hoisted block scan as [156](../156-wcsspn/) and [157](../157-wcscspn/): the first three set
members are broadcast **once** into `ymm2`/`ymm4`/`ymm5` before the loop, with spare slots taking a
duplicate of member 0 (harmless, because the compares are OR-ed and $a \lor a = a$), and any members
past the third walked from memory in a tail that costs two µops per block when it is empty.

The difference from a span is that this has to know **why** the scan stopped, not just where: a set
hit returns its address, the terminator returns NULL. So the hits and the terminator are kept in
**separate** masks, OR-ed only to locate the first stop, and `bt` then asks which of the two that
first stop was. Merging them would lose exactly the information the return value depends on.

A scalar early-out handles a hit at the very first character, where the vector prologue is pure
latency. Both operands are loaded independently so the two loads issue together, and the
"both are terminators" case is separated from a genuine hit by a single `test`.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe` compares the returned **pointer**, so "found the right character" and "found the
right occurrence of it" are both checked. **PASS** — **16 alignments × lengths 0..200 × set sizes
0..8**, plus a disjoint set at every length; a **single member planted at every position**; a member
on **each side of the terminator inside the same 32-byte block**, which is precisely the case the two
separate masks exist for (a member past the terminator must still return NULL); zero-low-byte,
zero-high-byte and `0xFFFF` wchar traps; and **NOACCESS page-guard sweeps on both the string and the
set**.

## Benchmark — vs live `ucrtbase!wcspbrk`
geomean **9.08×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars, 3-set | 4.45 | 23.80 | 5.35x |
| 64 chars, 3-set | 7.12 | 93.47 | 13.13x |
| 254 chars, 3-set | 15.79 | 346.99 | **21.97x** |
| 1024 chars, 3-set | 62.33 | 1374.88 | 22.06x |
| 254 chars, hit at 8 | 4.89 | 13.79 | 2.82x |
| 254 chars, 16-set | 117.87 | 686.58 | 5.82x |

### On the early-hit case, and why it is "hit at 8"
The early-hit case was originally a hit at index **0**, and it measured **3.33 ns for ours against
3.12 ns for live — 0.94×**. That number is reported here rather than quietly dropped, but it is not
usable as a gate, and the reason was measured rather than assumed: in this harness a **do-nothing
indirect call costs 3.11 ns**, so live's 3.12 ns on that input is the call overhead and nothing else,
and 3.33 vs 3.12 is a single quantisation step of an instrument that cannot resolve the difference.
Two different formulations of the early-out (a straight compare, and independent loads feeding one
compare) both produced exactly 3.33 ns, which is what a floor looks like.

Moving the hit to index 8 keeps the case an early exit — it still returns from the first block, via
the masked prologue — while putting it above the floor, where it measures **2.82×**. A hit at index 0
remains covered exhaustively in `correctness.exe`, at every alignment and every set size.

## Reproduce
```
changes\158-wcspbrk\build.bat
```
