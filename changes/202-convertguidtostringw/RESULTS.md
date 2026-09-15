# 202 `iphlpapi!ConvertGuidToStringW` — **LANDS** (37.30× geomean, up to 110.89×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `iphlpapi.dll` from the live
System32. First target in this project from `iphlpapi.dll`.

## Why this target

**The largest single win in the project**, and for an almost comic reason: *the shipped function does
not format the GUID.*

The disassembly at RVA `0x3F60` spills the eleven GUID fields to the stack as varargs, loads the
literal format string

```
{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}
```

and calls a `StringCchPrintfW` clone that **re-parses that format on every call** and dispatches each
conversion through a per-character output helper. It costs **~305 ns to write 38 characters** — about
8 ns per character, on a machine where this project routinely moves 30 GB/s.

## The permutation is the whole trick

`Data1`/`Data2`/`Data3` are little-endian integers printed most-significant-nibble first, while
`Data4` prints in memory order. So the sixteen GUID bytes appear in print order

$$3,2,1,0,\;5,4,\;7,6,\;8,9,10,11,12,13,14,15$$

which is **a single `vpshufb`**. After that the conversion is branch-free:

| step | instruction |
|---|---|
| put the bytes in print order | `vpshufb` |
| split high/low nibbles | `vpsrlw` + two `vpand` |
| interleave so 16 bytes become 32 nibbles in output order | `vpunpcklbw` / `vpunpckhbw` |
| map nibbles to `0`–`9`, `A`–`F` | `vpshufb` through a 16-entry table |
| widen those 32 characters to UTF-16 | `vpmovzxbw` |

The separators never take part: a 39-cell template is stored first and the five runs of hex are
written over its placeholder digits.

ISA: **AVX2 only** — no AVX-512, no GFNI, so it runs on Zen 3 and Zen 4 alike.

## The contract — three failure lengths, three different behaviours

The return value alone is not enough to pin this function down; a reimplementation can return the
right code and still be wrong about what it left in the buffer. Measured line by line against the live
export in `probes/cgs.c`:

| input | return | buffer |
|---|---|---|
| `Guid == NULL` or `String == NULL` | 87 `ERROR_INVALID_PARAMETER` | untouched |
| `cch == 0` | 122 `ERROR_INSUFFICIENT_BUFFER` | **untouched** |
| `1 ≤ cch ≤ 38` | 122 | **written**: the first `cch-1` characters, then a NUL at `[cch-1]` |
| `cch ≥ 39` | 0 | the full 38 characters + NUL |
| `cch ≥ 0x80000000` | 122 | `String[0] = 0` — **and 122, not 87** |

That last row is the trap. The inner helper rejects `(cch-1) > 0x7FFFFFFE` and the wrapper maps its
`E_INVALIDARG` to 122 like any other failure, so an implementation that treats an absurd length as a
bad parameter returns the wrong code. `cch == 38` is the other one: it stops one character short of
the closing brace.

## Correctness — PASS

Three-way (ours vs the scalar oracle vs the **live export**), comparing the return value **and the
whole 160-cell buffer**, including cells the function chose not to touch:

* 5 fixed GUIDs × every `cch` 0..64;
* 7 absurd lengths (`0x7FFFFFFE`, `0x7FFFFFFF`, `0x80000000`, `0x80000001`, `0xC0000000`, `0xFFFFFFFE`, `0xFFFFFFFF`);
* **every byte position × all 256 values × 3 buffer regimes** — this is what proves the print permutation;
* 16 unaligned GUID pointers;
* 300 000 random (GUID, length) pairs;
* a `PAGE_NOACCESS` guard page on **both** the output buffer and the 16-byte GUID load, proving no
  read or write passes its bound.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| cch 64 (typical) | 2.78 | 304.58 | **109.66×** |
| cch 39 (exact fit) | 2.82 | 302.87 | 107.45× |
| all zero, cch 64 | 2.82 | 312.65 | 110.89× |
| all FF, cch 64 | 2.82 | 305.34 | 108.30× |
| cch 20 (truncates) | 10.27 | 106.52 | 10.38× |
| cch 0 (no write) | 2.36 | 4.32 | 1.83× |

**geomean 37.30× → LANDS** (no size class regressed). 13.5 GB/s of output on a 78-byte string.

The truncating class is "only" 10× because both sides do less work: the system function stops
formatting early, and ours still renders all 38 characters into a stack scratch before copying the
prefix out. Rendering unconditionally is what keeps the common path branch-free, and it is still
10× ahead.

## Live substitution — PASS

`live-substitution/live_subst_iphlpapi.c`: validate-first against the live export over 200 000 cases,
then the export's prologue is hot-patched in a sacrificial single-threaded child (own-process COW
copy) and the same corpus is replayed. Return value and the whole buffer identical across all four
length regimes — 102 156 truncating, 15 425 zero-length, 20 081 absurd — the counter proving our code
ran 200 000 times, and the prologue restored byte-for-byte afterwards.

## Note: this change found the ABI bug

The first cut of this implementation used `xmm6`/`xmm7` as scratch. It was **bit-exact correct** and
its benchmark reported `0.00 ns`, because `xmm6`–`xmm15` are callee-saved under Win64 and the harness
keeps its timing accumulators there. That single accident exposed **sixteen implementations** in this
repository with the same latent defect, none of which any correctness or speed gate could see. See
[`tools/README.md`](../../tools/README.md) for the gate that now exists because of it.
