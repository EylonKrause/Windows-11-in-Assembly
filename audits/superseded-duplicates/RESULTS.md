# Ten exports had more than one landed change — **all ten resolved**

`image/materialize.py` wrote one `.asm` per export and kept whichever README row came **last**. For
ten exports that meant the image tree's provenance was decided by row order and nothing else. Change
279 removed that ambiguity for `RtlIntegerToChar` by marking change 097 superseded; this audit
finishes the job for the rest.

## The six genuine supersessions — settled by measurement

A headline geomean is **not** a comparison: two changes are benched on different row sets, at
different times, sometimes at different optimisation levels. Change 279 found a replacement that
read `BETTER` against ntdll on every row while *losing* to the change it replaced. So all three
sides — live ntdll, the older change and the newer one — were run over **one identical corpus**,
each in **its own process** (changes 030 and 259 both export `wia_arebitsset`, and 192 and 259 both
export `wia_arebitsclear`, so the two sides cannot be linked into one image at all), each printing a
hash of every answer plus a timing.

| export | old | new | hashes | shared-corpus timing |
|---|---|---|---|---|
| `RtlNumberOfSetBits` | 023 | **257** | identical | 14.4 → 8.5 ns (**1.71×**), live 20.4 |
| `RtlNumberOfClearBits` | 124 | **257** | identical | 14.4 → 9.4 ns (**1.67×**), live 20.9 |
| `RtlAreBitsSet` | 030 | **259** | identical | 8.0 → 8.0 ns (**1.00×**), live 15.6 |
| `RtlAreBitsClear` | 192 | **259** | identical | 8.0 → 8.0 ns (**1.00×**), live 11.9 |
| `RtlFindLongestRunClear` | 123 | **255** | identical | 448 → 103 ns (**4.33×**), live 354 |
| `RtlIntegerToUnicodeString` | 052 | **278** | identical | 114.0 → 111.3 ns (**1.02×**), live 125.3 |

**No defect in any of the six older changes.** Every one agrees with live ntdll on every case in the
corpus. They are retired so that one export maps to one implementation, nothing more.

### Two results worth stating plainly, because they contradict the headlines

- **`RtlAreBitsSet` and `RtlAreBitsClear` are the same speed.** The README claimed 3.17× for 030 and
  **12.32×** for 259; on a shared corpus they are 8.0 ns each. The difference is the size
  distribution each was benched on, not the code. The supersession here is justified by provenance
  — 259 covers both exports in one change — and **not** by a speed claim, and the README rows now
  say so.
- **Change 123 is SLOWER than live ntdll on this corpus** — 448 ns against 354 — despite a 4.79×
  headline. 255 does the same work in 103 ns. Same lesson, more sharply: a speedup is a statement
  about a distribution, and it travels badly.

## The four crypt32 groups were never duplicates

`CryptBinaryToStringA`/`W` and `CryptStringToBinaryA`/`W` each have four landed changes because each
takes a **format selector** and each format is its own implementation — base64, hexraw, hexfmt,
base64header. The one-`.asm`-per-export model simply could not express that, so three of every four
were written and then overwritten.

`materialize.py` now groups entries by `(dll, export)`. A single-contributor export materialises
exactly as before; a multi-contributor export gets **all** its bodies, under banners naming the
change each came from, with a header saying the file is a concatenation of independently built
sources rather than a translation unit. The per-DLL manifest links every contributor, and the script
**prints** the multi-contributor list on every run so this cannot go quiet again.

Before: 16 writes to 4 paths, 12 silently discarded. After: 4 files, 16 bodies, **0 deletions in the
tree**.

## The near-miss that is the real lesson

The first run of this audit reported that **change 052 disagreed with live ntdll** while change 278
agreed exactly. That is a serious claim about landed code. Narrowing it from a hash to actual cases
before writing it down produced this:

```
live  st=00000000 Length=18   0034 0034 0031 0031 0033 0037 0037 0036 0035 0000
052   st=00000000 Length=18   0034 0000 0000 0000 0000 0000 0000 0000 0000 0000
```

The **right length**, and an **all-zero buffer**. That is not how a formatter fails — it is how an
**uninitialised table** fails. Change 052 keeps its two-digit table in a C file as a bare array plus
a `wia_dec2_init()` that fills it, and its own `correctness.c` calls that on the first line of
`main`. The audit harness did not. With the initialiser called, all three hashes are identical.

This audit is about gates that fail to ask a question. That was a **harness that asked one it had
not set up for**, and it produced an equally confident and equally wrong answer. A near-miss report
of a defect in landed code is the same class of error as missing a real one. The only thing that
caught it was refusing to write down "052 is broken" without first looking at a single failing case.
`abdup.c` now takes `-DINITFN=<name>` and the driver passes it for any change that ships a runtime
table initialiser; `diff052.c` keeps the story.

## Reproduce
```
py audits\superseded-duplicates\run.py
```
