# 159 — `ucrtbase!strspn` — **LANDS** (8.60× geomean, up to 27.1×)

## This one wins for a different reason than the wide version did
The narrow set-scans are **not** the naive $O(nm)$ loop their wide siblings are. With only 256
possible byte values, `strspn` builds a 256-bit bitmap of the set and then walks the string one byte
at a time against it — 180 ns for 254 characters, against 404 ns for `wcsspn`
([156](../156-wcsspn/)). The $O(m)$ factor is already gone.

So the win here is not from removing an inner loop. It is from doing **32 characters per step
instead of one**, and from not building a table at all when the set is small enough to live in
registers. That is worth 20× at 254 characters even against an implementation that had already
solved the algorithmic problem.

## Method
The byte-granular twin of [156](../156-wcsspn/), structurally identical: the first three set
characters are broadcast **once** into `ymm2`/`ymm4`/`ymm5` before the block loop, so the loop body is
straight-line. Shorter sets fill the spare registers with a **duplicate of member 0** — harmless,
because the compares are OR-ed and $a \lor a = a$ — which avoids needing three specialised loops. An
empty set is answered up front (nothing is in it, so the span is 0), which is also what makes the
duplicate well defined: member 0 is guaranteed to exist past that point. Sets longer than three walk
the remainder from memory, a tail costing two µops per block when it is empty.

The terminator needs no special case: a set is itself NUL-terminated, so it can never contain NUL,
and the NUL therefore fails every compare and stops the span naturally. The complement span in
[160](../160-strcspn/) does not get that for free.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build — **32 alignments × lengths 0..200 × set sizes 0..8**
(a full 32-byte alignment sweep, since bytes have no alignment restriction), plus the full alphabet
at every length; a **single non-member planted at every position of every length**, which pins the
exact stopping point; **high-bit bytes `0x80..0xFF`**, which is where a sign-extension bug would show
because `char` is signed on MSVC; **sets of 128 and 255 members**, running well past the three
hoisted registers into the memory tail; and **NOACCESS page-guard sweeps on both the string and the
set** — the set matters because it is walked scalar-wise and must stop at its own NUL.

## Benchmark — vs live `ucrtbase!strspn`
geomean **8.60×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars, 3-set | 2.91 | 15.79 | 5.44x |
| 64 chars, 3-set | 4.89 | 53.64 | 10.96x |
| 254 chars, 3-set | 8.90 | 180.48 | 20.28x |
| 1024 chars, 3-set | 25.58 | 694.21 | **27.14x** |
| 254 chars, stops at 8 | 3.56 | 10.01 | 2.81x |
| 254 chars, 16-set | 41.37 | 181.93 | 4.40x |

The 16-member set is the weakest case, and honestly so: thirteen members fall past the hoisted
registers and are re-walked from memory on every block. Note that the live bitmap approach is flat in
set size (181.93 ns for 16 members vs 180.48 for 3) while ours is not — a large enough set would
eventually favour a bitmap, and this implementation does not pretend otherwise.

## Reproduce
```
changes\159-strspn\build.bat
```
