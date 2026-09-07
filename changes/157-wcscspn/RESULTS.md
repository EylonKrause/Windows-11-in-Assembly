# 157 — `ucrtbase!wcscspn` — **LANDS** (9.12× geomean, up to 29.4×)

The complement span — the length of the initial run of characters in **neither** the set nor `{NUL}`.
The live one is the naive $O(nm)$ scalar loop, 403 ns for 254 characters against a 3-character set,
while the narrow `strcspn` manages 139 ns with a 256-bit set bitmap that cannot be built for 65536
wide values.

## Contract
A probe confirmed that `ucrtbase!wcscspn` and `shlwapi!StrCSpnW` (change [136](../136-strcspnw/))
agree on every distinguishing edge case — **empty set → the whole string length**, empty string, no
match, and a set member with a zero low byte. Only the return type differs (`size_t` vs `int`).

## Method
The complement of [156](../156-wcsspn/), with one real difference: there the terminator needed no
special case, because a NUL can never be a member of a NUL-terminated set, so it stopped the span for
free. Here the span continues *while* characters are outside the set, so the NUL would **not** stop it
— it has to be compared explicitly and OR-ed into the stop mask.

The first three set members are broadcast once into `ymm2`/`ymm4`/`ymm5` before the block loop, spare
slots taking a duplicate of member 0 ($a \lor a = a$). An **empty set** fills all three with zero,
which merely duplicates the terminator compare that seeds the accumulator — and that is exactly the
right answer here: scan to the terminator. `ymm3` doubles as the compare scratch and is re-zeroed per
block with a `vpxor`, which is a zeroing idiom: renamed, not executed.

### Why hoisting mattered more than it looks
Change 136 re-walked the set inside every block. Beyond the instruction count, that made the routine
**badly sensitive to code layout**: adding three µops at the top of the function, or alignment padding
on the per-block fall-through, moved the 1024-character result between **96 and 125 ns with no change
whatever to the work done**. A branchy loop that small aliases in the branch predictor, and its cost
ends up decided by where the code happens to land. Chasing that was what surfaced the real fix — with
the set hoisted and the block body straight-line, the same input runs in 54.5 ns and the number stops
moving.

### The scalar early-out
When the first character already stops the scan the answer is 0, and the vector prologue
(load → compare → `vpmovmskb` → `tzcnt`) is pure latency that the live scalar loop beats outright.
A single compare against `set[0]` handles it. An empty set needs no guard: its first word is 0 while
the string's first character has just been shown to be non-zero, so the compare cannot hit.

Only `set[0]` is tested, deliberately. A version that walked up to four set members fixed the same
case but cost ~15% on **every** other class — a bad trade for one degenerate input.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** — **16 alignments × lengths 0..200 × set sizes 0..8**, plus a **disjoint
set at every length**, which is the case that requires the NUL in the stop mask and the one that would
run off the end if it were missing; a **single member planted at every position of every length**;
zero-low-byte / zero-high-byte / `0xFFFF` wchar traps; and **NOACCESS page-guard sweeps on both the
string and the set**.

## Benchmark — vs live `ucrtbase!wcscspn`
geomean **9.12×**, no size class regressed:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars, 3-set | 4.00 | 27.14 | 6.78x |
| 64 chars, 3-set | 5.78 | 101.86 | 17.61x |
| 254 chars, 3-set | 13.79 | 403.58 | **29.26x** |
| 1024 chars, 3-set | 54.50 | 1602.67 | 29.40x |
| 254 chars, member at 0 | 3.33 | 3.34 | 1.00x (~tie) |
| 254 chars, 16-set | 112.98 | 630.16 | 5.58x |

A note on reading that fifth row: a **do-nothing indirect call costs 3.11 ns in this harness**
(measured directly), so at 3.33 vs 3.34 ns both sides are within one quantisation step of the
measurement floor and the row is really reporting call overhead. It is worth having as a guard
against a genuinely slow early path — the pre-early-out version measured 3.78 vs 3.34, a real 0.88×
— but a 1.00× there should not be read as a meaningful tie.

## Reproduce
```
changes\157-wcscspn\build.bat
```
