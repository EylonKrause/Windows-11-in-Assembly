# 016 — `RtlUnicodeToUTF8N` (AVX2) — **LANDED** (core ntdll, 2.46× geomean over every input class)

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
(`vpackuswb` + a `vpermq 0xD8` lane fixup); an 8-wide path handles 8–15.

*Everything else* used to be exact scalar, and that turned out to be the whole problem — see
**The table used to be ASCII only** below. There are now three more blocks, covering one-or-two-byte
characters, any non-surrogate character, and surrogate pairs; the scalar path remains, and still
defines this function's exact overflow and lone-surrogate behaviour.

## The measuring mode — added 2026-09-16, because it was missing

`RtlUnicodeToUTF8N(NULL, 0, &produced, src, srcLen)` is the documented way to ask this function how
many bytes the output will need: the shipped export returns `STATUS_SUCCESS` with `produced` set and
writes nothing. This implementation returned `STATUS_BUFFER_TOO_SMALL` with `produced = 0`, and with
a NULL pointer and a **non-zero** size it dereferenced the pointer and faulted.
`discovery/utf8n_null_destination.c` is the evidence, committed separately in `8bfd5db`; the fix is
commit `417f31f`.

**Why the gates did not catch it.** This change is bit-exact against the live export at every real
capacity from 0 upward — and every case in those corpora passes a *real* destination buffer. A NULL
destination is not an edge of the LENGTH, which is what the corpora sweep; it is a different **mode**
of the same function, and nothing asked for it.

**The fix** is a counting pass taken when the destination is NULL, using the same rule the encoder
already implements: below `0x80` is one byte, below `0x800` is two, a high surrogate followed by a
low one is four and consumes both, and everything else — including a lone surrogate, which becomes
`U+FFFD` — is three, with a lone surrogate also setting `STATUS_SOME_NOT_MAPPED`. That rule was
verified against the live measuring mode over 200,000 random strings, at 0 disagreements on the size
and on the status, before a line of assembly was written.

`measuring.c` gates it: **122,006 cases, 0 mismatches** — every length from 0 to 400 for ASCII,
two-byte, three-byte, surrogate-pair and lone-surrogate inputs, the NULL-with-non-zero-size case that
used to fault, and 120,000 randomised. It also checks that the measured size is exactly the size a
real conversion produces. The live measuring mode answered `SUCCESS` 64,897 times and
`SOME_NOT_MAPPED` 57,108, so both statuses are exercised.

**What it costs, measured rather than waved away.** The two-instruction test at the entry costs
**0.12 ns** on the 8-byte row — 2.93 ns before, 3.05 after — moving the geomean on this machine from
2.68× to 2.62×. Moving the test *after* the prologue, so that it might issue alongside the seven
pushes, was tried and measured **worse** at 3.13 ns; the entry is the better of the two placements.
Every size class still beats the shipped code and the change still **LANDS**.

## The table used to be ASCII only — 2026-09-16

For a UTF-8 **encoder**, that was the wrong table to publish. Every row was ASCII input, which is the
one case the original fast path handled, so the 2.82× it reported was the speed of a path a caller
converting Hebrew, Greek, Cyrillic, CJK or emoji never reaches.
`discovery/utf8_nonascii_rows.c` (commit `95ae7b3`) asked the same function about the input UTF-8
exists for and got **0.21× to 0.94×** — geomean **0.798×** over 24 rows, which is PARKED.

Two things were wrong, and they were fixed separately because they deserve separate evidence.

**1 — A control-flow bug, fixed in `e71db44`.** The scalar walk jumped back to the top of the loop
after *every* character and paid for both vector blocks again, which is
[change 263's rule](../263-rtlcompareunicodestrings/RESULTS.md) — *a scalar walk must not re-enter a
vector loop* — broken here. On alternating input that is two vector loads and two tests per
character, which is why `mixed` was the worst row in the table. A 16-character scalar **window**
amortises the probe over a cache line of input instead of over one character: 0.798× → 0.983×.

**2 — A missing kernel, fixed here.** What was left was not a mistake but work not done: ntdll
converts `mixed` at about a cycle and a half per character and is plainly vectorised, while a scalar
path costs five and a half. Three blocks now cover everything the function distinguishes:

| block | takes | costs | covers |
|---|---|---|---|
| 16-wide ASCII (existing) | 16 characters all `< 0x80` | ~4 instructions | ASCII |
| **one-or-two-byte** | 8 characters all `< 0x800` | ~18 instructions | 2-byte, mixed |
| **general BMP** | 8 non-surrogate characters | ~47 instructions | 3-byte, anything mixed |
| **surrogate pairs** | 4 valid pairs | ~20 instructions, no shuffle | supplementary planes |

The two packing blocks build every candidate byte of a character in its own lane and then compact
the lanes with one `VPSHUFB`, indexed by the characters' **lengths** — one bit per character for the
one-or-two-byte block, two for the general one. Both shuffle tables are generated by `REPT`/`IF` **at
assembly time**, so the source states the rule rather than 4096 pasted numbers, and `correctness.c`
checks the assembler's arithmetic against the same rule written in C.

The surrogate block needs no table at all: every valid pair is exactly two characters in and four
bytes out, so four of them are sixteen bytes with no length code and no shuffle. Masking with
`0xFC00` and comparing against an alternating `[D800, DC00]` pattern establishes in one `VPCMPEQW`
that all eight characters are surrogates *and* that they alternate high-low starting here. The
arithmetic is one `VPMADDWD`: subtracting that same pattern leaves each half in `0..0x3FF`, and
multiplying by `[0x400, 1]` and summing adjacent pairs **is** `(hi − 0xD800)·0x400 + (lo − 0xDC00)`.

### The surplus bytes, and why change 268 found them and this gate did not

Both packing blocks compute sixteen bytes and then advance by however many were *wanted*. Storing
all sixteen is the cheap way to finish, and inside the destination's capacity it is not a memory
error -- but it puts **zeros in the caller's buffer past the end of the string**, and the shipped
export leaves those bytes exactly as the caller left them.

Nothing here noticed, because this gate compared only up to the produced **length**, which is the
natural thing to compare and is not enough. [Change 268](../268-rtlunicodestringtoutf8string/)
compares its *whole* destination -- its wrapper's contract includes what a failing call leaves
behind -- and the first time it was built against these blocks it reported **154 mismatches**, every
one a single `00` where ntdll had left the caller's fill. Both gates now compare the whole capacity.

**The first fix was to blend, and it was measured and thrown away.** Reading the sixteen bytes back,
keeping whatever the output did not reach, and storing the result is four instructions and no
table -- and it cost **2.9x on three-byte input**, because every iteration's read overlaps the
previous iteration's store by a few bytes. A partially overlapping load cannot be forwarded from the
store buffer, so each one waits for the store to reach L1.

**What ships never reads the destination.** Exactly *L* bytes go out as two **overlapping** stores --
the first eight and the last eight -- which together cover `[0, L)` precisely when `L >= 8`; below
eight the same trick works with two 4-byte stores, which is the only branch. A third
assembler-generated table makes the second store possible: entry *k* shuffles byte `k+i` down to
position *i*, bringing the tail to where an 8-byte store will emit it.

It costs about 10% -- geomean 2.735x to 2.461x over the 24 rows -- and that is the honest price of
not writing into bytes the caller did not ask us to touch.

### One trap, and it would have mangled ordinary text

The first draft of the general block read the one-byte form out of the three-byte lane's last byte —
`0x80 | (c & 0x3F)` masked down to `c`. That is wrong for every ASCII character above `0x3F`: it
drops bit 6, so `A` (`0x41`) would have come out as `0x01`. It only shows up in a string that mixes
ASCII with non-ASCII, because a pure-ASCII string never reaches this block. The lane now carries `c`
itself in a fourth byte, and the mutation that reintroduces the bug is in the mutation suite.

## Correctness — PASS

**133,947 cases**, three-way against a scalar reference and live `ntdll`: the original 80,000-case
randomised fuzz, plus — new, and necessary — **runs**: pure ASCII, pure two-byte, pure three-byte,
pure surrogate pairs, ASCII alternating with two-byte, and pairs alternating with ASCII, at every
length from 0 to 200 and at every destination capacity from 0 to 3× the length, each with and
without a lone surrogate planted in it, plus both packing tables against the rule in C.

The random fuzz alone was **not enough** once the blocks existed: it draws each character's class
independently, so four consecutive valid surrogate pairs has a probability of about 4 × 10⁻¹¹ per
position. The surrogate block would have been entirely untested by it — and it passed that gate the
first time it was assembled, for exactly that reason.

The gate also now checks that **nothing is written at or past `dstMax`**. It did not before: the
comparison stopped at `min(len, dstMax)` and the destination was a 3000-byte array, so an
implementation that wrote eight bytes past its capacity wrote them into slack nothing looked at.

**Mutation-tested, 19 mutants, all 19 caught**: a shift off by one in each block, the one-byte blend
reversed, the `0xC0` lead marker dropped, the two-byte lead fixup removed, the surrogate test made to
accept anything, the `0x10000` offset dropped, the two halves of the general block swapped, each
block's destination guard halved, and a wrong entry in each of the two assembler-generated tables.

One mutant — halving the surrogate block's room guard — was **not** caught at first, and the reason
was worth fixing in the implementation rather than the test: the general block checked for room
*before* it checked for surrogates, so its 32-byte guard was also guarding the surrogate block, whose
own 16-byte guard could never fail. A check that cannot fail reads like a safeguard and is not one.
Testing the characters first costs nothing — the load happens either way — and gives the surrogate
block back a guard that means something.

## Speed — LANDS (no size class regressed)

Min-of-100, every input class, generous destination. **This table is the fix for the ASCII-only one
it replaces.**

| class | 64 | 512 | 4000 | 32000 |
|---|---:|---:|---:|---:|
| ASCII | 2.42× | 3.12× | 3.35× | 3.35× |
| 2-byte (`U+00A0`…) | 3.36× | 2.64× | 2.20× | 2.41× |
| 3-byte (`U+20A0`…) | 2.71× | 2.26× | 2.21× | 2.13× |
| surrogate pairs | 3.28× | 3.10× | 3.02× | 3.00× |
| mixed ASCII + 2-byte | 2.07× | 1.51× | 1.46× | 1.41× |
| lone surrogates (scalar on purpose) | 2.31× | 2.47× | 2.50× | 2.50× |

**Overall geomean 2.461× faster over 24 rows. Worst class 1.41×. No size class regressed → LANDS.**

Against the published ASCII-only numbers, and against the same 24 rows before this work:

| | before | after the window (`e71db44`) | with the blocks | with the exact store |
|---|---:|---:|---:|---:|
| geomean, 24 rows | 0.798× | 0.983× | 2.735× | **2.461×** |
| worst row | 0.21× | 0.28× | 1.75× | **1.41×** |

The last column is the one that ships: the third is what the blocks did while they were still
writing zeros past the end of the string.

## Iteration (the "don't give up" widening)

The first ASCII path packed 8 wchars → 8 bytes (128-bit) and landed at 1.83×. Widening to 16 wchars → 16
bytes (256-bit `vpackuswb` + `vpermq` lane fixup) raised it to **2.82×** on ASCII — and then the
measurement above showed that ASCII was the only thing that had ever been asked.

## Reproduce
```
changes\016-rtlunicodetoutf8n\build.bat
```
and for the measuring mode:
```
ml64 /c impl.asm & cl /O2 measuring.c impl.obj /Fe:measuring.exe & measuring.exe
```
