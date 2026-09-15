# 205 `rpcrt4!UuidFromStringA` — **LANDS** (7.45× geomean, up to 23.30×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `rpcrt4.dll`. First target in
this project from `rpcrt4.dll`.

## Why this target

`UuidFromStringA` measures **82.39 ns** against its own wide sibling `UuidFromStringW`'s **23.33 ns**
for identical work. A 3.5× penalty for the *narrower* input is the signature of a wrapper that widens
its argument and calls the wide path — and this project's own wide GUID parser
([change 118](../118-rtlguidfromstring/), `ntdll!RtlGUIDFromString`) already does the whole job in
~12 ns.

So the narrow form was not slow because parsing is hard. It was slow because it was not parsing.

## The contract — and it is the OPPOSITE of ntdll's on the one detail that matters

Every line measured against the live export in `probes/ufs.c`:

| input | return | output GUID |
|---|---|---|
| exactly 36 chars, **unbraced**, hex any case, `-` at 8/13/18/23, NUL at [36] | 0 `RPC_S_OK` | filled |
| **braced** `{…}` | **1705** `RPC_S_INVALID_STRING_UUID` | **untouched** |
| `StringUuid == NULL` | **0 — success** | the **nil UUID** (16 zero bytes) |
| one short, one long, bad hex, wrong separator, spaces, leading/trailing whitespace, trailing junk | 1705 | **untouched** |

Two of those rows are traps:

* **Braced strings are rejected here.** Change 118's `ntdll!RtlGUIDFromString` *requires* the braces.
  Getting the two contracts the wrong way round would be an easy mistake and an invisible one, since
  both would still return "an error" on the input the other accepts.
* **A NULL pointer is success**, returning the nil UUID — not `RPC_S_INVALID_STRING_UUID`, and not a
  fault. rpcrt4 really does treat a null string as "the nil uuid".

And the output must be **left untouched on failure**: a pre-poisoned GUID came back byte-identical
from all eleven malformed shapes tried. That rules out the shape change 118 uses — it writes each
byte into the caller's GUID as it parses — so this implementation accumulates into a stack scratch
and stores only once the string is known good.

## Method

32 characters go through a 256-entry table mapping hex digits to their value and **everything else,
NUL and `-` included, to `0FFh`**.

**Validation is branchless.** All 32 looked-up values are OR-ed into one accumulator, and a single

```
test acc, 0F0h
```

catches any invalid character: a real nibble only ever sets bits 0–3, so one `0FFh` anywhere leaves a
high bit set. That replaces 32 conditional branches with one test.

**Page safety.** The fast path reads all 37 bytes (0..36) *before* it knows the string is that long —
harmless inside a mapped page, a fault across the end of one. So the page offset is checked first:
within 37 bytes of a boundary the code walks the string for its terminator in a bounded 37-byte scan
and only then parses, by which point all 37 bytes are provably readable. Same discipline as the
unbounded string scans in changes 001–004.

Byte order is the same $3,2,1,0,\;5,4,\;7,6,\;8..15$ permutation the formatters in changes 202/203
apply in the other direction, so `"deadbeef-1234-5678-9abc-def011223344"` becomes
`EF BE AD DE 34 12 78 56 9A BC DE F0 11 22 33 44`.

ISA: baseline x64. No SIMD — 32 table lookups are two dependent loads each, and at 2–3 loads per
cycle the parse is load-throughput bound well under what a shuffle-based nibble packer would cost to
set up for a 36-byte input whose hex digits are not even contiguous.

## Correctness — PASS

Three-way (ours vs the scalar oracle vs the **live export**), comparing the return value **and all
sixteen output bytes on every case, failing ones included**:

* the NULL-pointer success case;
* 28 hand-picked shapes — braced, half-braced, short, long, bad hex at either end, each wrong
  separator, spaces for separators, leading and trailing whitespace, trailing junk;
* an **exhaustive single-character corruption sweep**: 36 positions × 255 byte values, which proves
  the separator positions and the hex table together;
* every truncation length 0..40;
* 400 000 fuzz cases;
* a **`PAGE_NOACCESS` guard** with strings of every length 0..38 ending exactly at the boundary —
  which is what actually exercises the 37-byte page-offset guard and its bounded fallback scan. A
  wrong guard faults here rather than merely disagreeing.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| valid lower | 8.71 | 80.62 | 9.25× |
| valid UPPER | 8.88 | 82.56 | 9.29× |
| braced (rejected) | 2.72 | 63.45 | **23.30×** |
| truncated (rejected) | 8.62 | 79.71 | 9.24× |
| NULL (nil uuid) | 2.72 | 2.72 | 1.00× |
| mixed ×64 | 633.62 | 5846.88 | 9.23× |

**geomean 7.449× → LANDS** (no size class regressed).

The braced class is the widest win because rejection is nearly free here — the `'{'` fails the first
hex lookup — whereas the shipped code still widens the whole string before discovering the problem.
The NULL class is a tie at 2.72 ns: both sides do nothing but write sixteen zero bytes.

## Live substitution — PASS

`live-substitution/live_subst_rpcrt4.c`: 200 000 cases, validate-first, then the export's prologue
hot-patched in a sacrificial single-threaded child. **141 233** parsed, **58 767** rejected and
**5 406** were the NULL pointer, so all three paths ran in bulk under the patch. Return value and all
sixteen output bytes identical, with the output proven untouched on every failure; prologue restored
byte-for-byte.
