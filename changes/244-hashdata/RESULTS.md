# 244 `shlwapi!HashData` / `kernelbase!HashData` — **LANDED** (2.58× geomean, up to 3.98×; worst class 2.10×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Five consecutive runs, every one of them `LANDS`: per-run geomean 2.576 / 2.580 / 2.581 / 2.589 /
2.592, worst size class 2.10×.

## Verdict

Bit-exact over 169 115 correctness cases and 29 919 live cases, ABI-clean, and faster on **every**
one of fourteen size classes spanning both of this function's size parameters.

This is the first change in the repository whose target was found by looking at what the project had
never measured. `discovery/shlwapi_url_str.c` was written to sweep three whole shlwapi families the
earlier surveys skipped — the URL functions, the formatters and parsers, and the remaining path
predicates and writers — and `HashData` came out of it as the cleanest target it found: 26 102 ns to
hash 4096 bytes into a 16-byte digest, **0.157 GB/s**, with no locale, no code page, no path grammar
and no allocation anywhere in it.

## Why this target, precisely

`probes/cost.c` measured the whole two-dimensional cost surface rather than one shape, and the answer
is unusually clean — the shipped cost is **one table lookup per (source byte × digest byte), about
1.9 cycles each**:

| cbData | cbHash | ns | ns/source byte | **ns per lookup** |
|---|---|---|---|---|
| 4096 | 6 | — | 2.46 | 0.4105 |
| 4096 | 16 | 26 102 | 6.37 | 0.4168 |
| 4096 | 32 | 54 530 | 13.81 | 0.4316 |
| 4096 | 128 | 211 670 | 53.34 | 0.4168 |
| 4096 | 4 | 9 036 | 2.20 | 0.5508 |
| 4096 | 2 | — | 2.11 | 1.0546 |
| 4096 | 1 | 8 690 | 2.02 | **2.0169** |

Flat from six digest bytes upward, and **rising as the digest shrinks** — which is the part that
shaped the implementation. At `cbHash == 1` there is one chain and no parallelism at all, and the
shipped loop's memory round trip becomes the limit rather than the lookup.

## The contract — `probes/hash.c`, measured against the live export

```
    h[j] = (BYTE)j                                for j = 0 .. cbHash-1
    for i = cbData-1 down to 0:
        for j = cbHash-1 down to 0:
            h[j] = T[ h[j] ^ src[i] ]
```

Every part of that is a measurement, not a reading:

| | how it was established |
|---|---|
| **the table `T`** | recovered in **closed form**: with a one-byte source and a one-byte digest the whole computation is a single substitution, so 256 calls give all 256 entries. It is a permutation of 0..255, and **0 of 256 entries** differ from the table in the shipped binary at RVA `0x2A6010` |
| **the seed** | `cbData == 0` consumes nothing, so the buffer IS the seed: `h[j] = (BYTE)j` for every `cbHash` 1..300 — and it **wraps**, `h[256] = 0x00` at `cbHash = 300` |
| **the order** | all **65 536** two-byte sources: last-byte-first matched 65 536, first-byte-first matched only the **256 palindromes**, neither matched 0 |
| **independent lanes** | `h[j] == T[j ^ src[0]]` for every `j` over every one-byte source, and the first four bytes of a 32-byte digest equal a 4-byte digest over 512 random 37-byte sources |
| **`NULL` anything** | `0x80070057` (E_INVALIDARG), digest untouched |
| **`cbHash == 0`** | returns S_OK and **writes nothing** — proved against a `PAGE_NOACCESS` digest pointer, not by inspecting a poison fill |
| **`cbData == 0`** | **reads nothing** — proved against a `PAGE_NOACCESS` source pointer |
| the model | 8512 exhaustive small shapes and 300 fuzz cases to 4000 × 300: **0 mismatches** |

## What makes it fast, and the one thing that makes it unsafe

Digest byte `j` depends only on itself and the source byte, so the digest bytes are **independent
chains**. The shipped loop nonetheless walks **one lane at a time through memory**: per lane it loads
`h[j]`, **reloads** `src[i]`, xors, loads the table and stores `h[j]` back — five memory operations
and about eight uops for one byte of progress. This implementation holds the lanes **in registers**
and advances them together, so a lane costs one xor and one table load, and the source byte is loaded
once for the whole group.

**Twelve is the widest group the register file allows**: the loop needs the table base, the source
pointer and the current source byte, which is three of the fifteen usable general registers.

### Two kernels, because "always twelve" regressed a class

A pass over the source costs the same whether it advances two lanes or twelve — it is bound by its
own dependency latency, not by twelve table loads — so the first version always used twelve. That is
not free in a throughput measurement, where independent calls overlap and the **uop count** binds:
twelve lanes is twenty-seven uops per source byte, and for a one-byte digest twenty-six of them are
thrown away.

| | always twelve lanes | exactly `cbHash` lanes |
|---|---|---|
| 16 B → 1 | 25.39 ns, **0.72×** | 8.24 ns, **2.22×** |
| 16 B → 2 | 25.67 ns, 0.99× | 10.96 ns, 2.33× |
| 16 B → 4 | 26.17 ns, 1.36× | 15.86 ns, 2.25× |
| geomean | 1.872× (**PARKED**) | 2.589× (LANDS) |

So `cbHash` 1..4 each get their own **leaf kernel** — no saved registers, no frame, six to twelve
uops per source byte — and the entry point is itself a leaf that tail-jumps into the framed
twelve-lane procedure only when there are five or more lanes to advance.

A second, smaller version of the same effect: a **last group of four or fewer** gets a four-lane
kernel rather than the twelve-lane one. `cbHash = 16` — the shape this function is actually called in
— is exactly one full group plus four, so that is not a corner case but half of the common case:
`4K → 16` went 2.67× → **2.91×** and `16B → 16` 2.10× → **2.51×**.

### The overlap fallback is required, not defensive

`probes/overlap.c` ran a 24-byte source against a 20-byte digest at **all 2401 relative placements**
inside one buffer:

```
    descending lanes, source re-read per lane : 2401 of 2401
    ascending lanes                           :  761
    the grouped shape used on the fast path   :  760   <- exactly the DISJOINT placements
```

The grouped shape is right on every disjoint placement and **wrong on all 1641 overlapping ones**,
because the shipped inner loop re-reads `src[i]` for every lane and a digest write that lands on
`src[i]` changes what the remaining lanes of that same source byte consume. So the implementation
tests whether the two ranges intersect and, when they do, emulates the shipped loop byte for byte —
seed written to the caller's buffer first, descending lanes, source re-read per lane.

That same measurement is what licences the fast path at all: on every disjoint placement all three
models agree.

### The seed is not written on the fast paths

Every digest byte is stored at the end of its group, so seeding the buffer first would be a write
immediately overwritten — unobservable while the buffers are disjoint, which the overlap test has
already established. The seed **is** written, on its own, when `cbData == 0`, because then it is the
entire result.

## Why the hot loop is scalar

A 256-entry byte substitution has no vector form cheaper than a load. The `vpshufb` construction for
a full 256-byte table costs sixteen shuffles plus sixteen compares and blends per sixteen lanes,
which is worse than sixteen loads; `vpgatherdd` is slower still on Zen 4 and its latency lands
squarely on the chain. AVX2 is used for the seed write only.

There is also no algebraic shortcut: `T` is **not affine** over GF(2)⁸ — `T[1] ^ T[2] ^ T[0]` is
`0x60` where an affine map would need `T[3] = 0x61`, and `T[3]` is `0x19` — so the composition of
substitutions cannot be folded, and the digest bytes must each take one lookup per source byte. The
floor is therefore `cbHash` lookups per source byte, which is exactly what both implementations do;
the win is entirely in what surrounds them.

## Gate 1 — correctness: **PASS** (169 115 cases)

Three-way on every case — ours, an independent oracle (`reference.c`) and the **live export** — and
the comparison is the **whole buffer against a poison fill plus a 32-byte canary past the digest**,
never just the digest bytes. Three measured facts make anything less insufficient: `cbHash == 0`
writes nothing at all, `cbData == 0` writes the seed and nothing else, and the implementation stores
its digest twelve bytes at a time, so an off-by-one in the partial-group store writes past `cbHash`
where only a canary sees it.

* `cbData` 0..8 × `cbHash` 0..30 × 48 fills, exhaustively;
* every byte value as a one-byte source at seven digest lengths, including 13 and 25 — the first byte
  of the second and third groups;
* every digest length 0..160, crossing both kernel boundaries and every group boundary;
* every source length 0..300 at three digest lengths;
* 120 fuzz cases to `cbData` 4000 × `cbHash` 300;
* every `NULL` combination;
* **the full overlap sweep** — every relative placement of six source and six digest lengths inside
  one buffer: **51 084 overlapping** and 101 016 disjoint;
* **guard pages on both sides** at every length 1..200: the digest ending on the last writable byte
  of a page, and the source ending on the last readable one;
* seed-only digests to 1100 bytes, which drives the seed writer's vector blocks and its byte tail.

## Gate 2 — speed: **PASS**

| case | ours ns | shlwapi ns | ratio | ours GB/s |
|---|---|---|---|---|
| 16 B → 1 | 8.24 | 18.29 | 2.22× | 1.94 |
| 16 B → 2 | 10.96 | 25.54 | 2.33× | 1.46 |
| 16 B → 4 | 15.86 | 35.71 | 2.25× | 1.01 |
| 16 B → 16 | 43.40 | 108.73 | 2.51× | 0.37 |
| 64 B → 1 | 41.96 | 101.56 | 2.42× | 1.53 |
| 64 B → 16 | 148.09 | 418.72 | 2.83× | 0.43 |
| 256 B → 1 | 229.86 | 515.97 | 2.24× | 1.11 |
| 256 B → 4 | 254.47 | 562.45 | 2.21× | 1.01 |
| 256 B → 16 | 570.70 | 1634.78 | 2.86× | 0.45 |
| 4 K → 1 | 3 987.50 | 8 773.44 | 2.20× | 1.03 |
| 4 K → 4 | 4 016.41 | 9 029.69 | 2.25× | 1.02 |
| 4 K → 16 | 8 967.19 | 26 075.00 | 2.91× | 0.46 |
| 4 K → 32 | 14 598.44 | 54 784.38 | 3.75× | 0.28 |
| 4 K → 128 | 53 350.00 | 211 915.62 | **3.97×** | 0.08 |

**geomean 2.589×**, worst class **2.10×** over five runs. GB/s counts SOURCE bytes, so a wider digest
reads as slower on both sides — the work is the product of the two sizes, not either one.

The small-digest rows are in the table on purpose and at both ends of the source-length range: they
are the shapes a twelve-lanes-in-registers implementation is weakest on, and they are what the two
extra kernels exist for. **No restore is needed anywhere in this benchmark** — the digest is written
to a buffer that is never read back and the source is never modified — so none of the restore
artefacts that parked changes 142, 228, 230 and 241 can arise here. The destination still rotates
across four buffers, because it costs nothing and removes the question.

## Gate 3 — Win64 ABI: **PASS**

`tools/abi-check` (`T_244`): all eight non-volatile GPRs and the low 128 bits of xmm6–xmm15
preserved, stack balanced, direction flag clear — across all four leaf kernels, the framed twelve-lane
kernel, its four-lane last group, a 1000-byte digest (many passes), the seed-only path with both its
vector blocks and its byte tail, an **overlapping** call (the only path that writes the digest through
memory), and every `NULL` combination.

## Gate 4 — live substitution: **PASS** (29 919 cases, 0 mismatches)

`live-substitution/live_subst_kernelbase.c` hot-patches **`kernelbase!HashData`** — the body both
names reach, since `shlwapi!HashData` is a jmp thunk through `api-ms-win-core-url-l1-1-0` into it — in
a sacrificial single-threaded child's own copy-on-write copy, validate-first, and verifies the revert
byte-for-byte. Of 29 919 cases: 1052 through the leaf kernels, 1854 through the framed twelve-lane
kernel, 106 seed-only, 8 with `cbHash == 0`, and **10 076 overlapping placements against 16 820
disjoint ones** — the sweep that proves the fallback, since the grouped fast path is measurably wrong
on the former.

## Reproduce

```
changes\244-hashdata\probes\hash.c        the contract, incl. recovering the table in closed form
changes\244-hashdata\probes\cost.c        the shipped cost surface across both size parameters
changes\244-hashdata\probes\overlap.c     the 2401-placement sweep that fixes the loop order
changes\244-hashdata\build.bat            correctness + bench
```

## ISA and portability

AVX2 for the seed write; the hot loops are base integer only. Runs on Zen 3 and Zen 4 alike.
