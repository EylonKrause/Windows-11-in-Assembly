# 161 — `ucrtbase!strpbrk` — **LANDS** (5.75× geomean, up to 19.4×)

Pointer to the first character of the string that appears in the set, or NULL. Like its two
neighbours the live one builds a 256-bit bitmap of the set and then walks the string a byte at a time
against it: 132 ns for 254 characters.

## Method
The byte-granular twin of [158](../158-wcspbrk/): the first three set characters are broadcast
**once** into `ymm2`/`ymm4`/`ymm5` before the loop, spare slots taking a duplicate of member 0
(harmless, since the compares are OR-ed and $a \lor a = a$), and members past the third walked from
memory in a tail costing two µops per block when it is empty.

The difference from a span is that this has to know **why** the scan stopped, not just where: a set
hit returns its address, the terminator returns NULL. So the hits and the terminator are kept in
**separate** masks, OR-ed only to locate the first stop, and `bt` then asks which of the two that
first stop was. Merging them would destroy exactly the information the return value depends on — a
set member sitting one byte past the terminator, in the same 32-byte block, must still return NULL.

A scalar early-out handles a hit at the very first character. Both operands are loaded independently
so the two loads issue together, and the "both are terminators" case is separated from a genuine hit
by a single `test`.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe` compares the returned **pointer**, so "found the right character" and "found the
right occurrence of it" are both checked. **PASS** on the first build — **32 alignments × lengths
0..200 × set sizes 0..8**, plus a disjoint set at every length; a **single member planted at every
position**; a member on **each side of the terminator inside the same 32-byte block**, which is
precisely the case the two separate masks exist for; **high-bit bytes** and a **255-member set**; and
**NOACCESS page-guard sweeps on both the string and the set**.

## Benchmark — vs live `ucrtbase!strpbrk`
geomean **5.75×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars, 3-set | 3.56 | 11.79 | 3.31x |
| 64 chars, 3-set | 4.89 | 34.93 | 7.14x |
| 254 chars, 3-set | 8.90 | 131.64 | 14.79x |
| 1024 chars, 3-set | 24.97 | 485.30 | **19.43x** |
| 254 chars, hit at 8 | 4.23 | 9.12 | 2.16x |
| 254 chars, 16-set | 57.62 | 141.86 | 2.46x |

The early-hit case is at index 8 rather than 0 for the reason measured in
[158](../158-wcspbrk/): a do-nothing indirect call costs 3.11 ns in this harness, so a hit at index 0
times the call and not the code. A hit at index 0 stays covered exhaustively in `correctness.exe`.

## Reproduce
```
changes\161-strpbrk\build.bat
```
