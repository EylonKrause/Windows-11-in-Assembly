# 203 `iphlpapi!ConvertGuidToStringA` — **LANDS** (35.58× geomean, up to 97.16×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `iphlpapi.dll` from the live
System32. The narrow sibling of [change 202](../202-convertguidtostringw/RESULTS.md).

## Why this target

Same cause as 202: **the shipped function does not format the GUID.** It spills the eleven fields to
the stack as varargs, loads the literal format string

```
{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}
```

and calls a `StringCchPrintfA` clone that re-parses it on every call, dispatching each conversion
through a per-character output helper. Measured at **263.44 ns per call** to write 38 characters.

## The contract was measured, not inherited

This is a **separate export** at a different address (`0x…2890` against the wide form's `0x…3F60`),
and this project has repeatedly found the A and W forms of a routine carrying different conventions.
So `probes/cgsa.c` drove both exports over **200 000 random (GUID, length) pairs** and compared them
character for character:

> **0 return-value differences, 0 buffer differences.**

The narrow contract is the wide one with byte cells:

| input | return | buffer |
|---|---|---|
| `Guid == NULL` or `String == NULL` | 87 | untouched |
| `cch == 0` | 122 | **untouched** |
| `1 ≤ cch ≤ 38` | 122 | the first `cch-1` characters, then a NUL at `[cch-1]` |
| `cch ≥ 39` | 0 | the full 38 characters + NUL |
| `cch ≥ 0x80000000` | 122 | `String[0] = 0` — **not 87** |

One ordering detail the probe settled: the **NULL test comes first**, so `(NULL, NULL, 0)` returns
87, not 122.

## This one is cheaper than the wide form

Change 202 has to widen: `vpmovzxbw` twice plus two `vextracti128` to turn 32 ASCII bytes into 32
UTF-16 cells. Here **the `vpshufb` output is already the answer**, so the tail is five stores:

```
vmovq   [rdi + 1],  xmm3      ; hex[0..7]   -> out[1..8]
vpextrd eax, xmm3, 2          ; hex[8..11]  -> out[10..13]
vpextrd eax, xmm3, 3          ; hex[12..15] -> out[15..18]
vmovd   eax, xmm4             ; hex[16..19] -> out[20..23]
vpsrldq xmm5, xmm4, 4
vmovq   [rdi + 25], xmm5      ; hex[20..27] -> out[25..32]
vpextrq rax, xmm4, 1          ; hex[24..31] -> out[29..36]  (overlapping the previous store)
```

The last two overlap deliberately on `out[29..32]`, which writes the same four bytes twice and costs
nothing, rather than masking out a 12-byte run.

Everything before that is identical to 202 — one `vpshufb` puts the sixteen GUID bytes in print order
$3,2,1,0,\;5,4,\;7,6,\;8..15$, the nibbles are split and interleaved, and a second `vpshufb` maps them
through a 16-entry hex table. A 39-byte template is stored first so the braces and separators never
take part.

**Only `xmm0`–`xmm5` are touched** — `xmm6`–`xmm15` are callee-saved under Win64; the gate in
[`tools/abi-check`](../../tools/abi-check/) confirms it.

## Correctness — PASS

Three-way (ours vs the scalar oracle vs the **live export**), comparing the return value **and the
whole 160-byte buffer**, including bytes the function chose not to touch: 5 fixed GUIDs × every `cch`
0..64; 7 absurd lengths; **every byte position × all 256 values × 3 buffer regimes** (which is what
proves the print permutation); 16 unaligned GUID pointers; 300 000 fuzz cases; and a `PAGE_NOACCESS`
guard page on both the output buffer and the 16-byte GUID load.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| cch 64 (typical) | 2.70 | 262.06 | **97.16×** |
| cch 39 (exact fit) | 2.82 | 261.44 | 92.73× |
| all zero, cch 64 | 2.80 | 272.38 | 97.11× |
| all FF, cch 64 | 2.82 | 267.55 | 94.90× |
| cch 20 (truncates) | 6.81 | 95.63 | 14.03× |
| cch 0 (no write) | 2.35 | 4.09 | 1.74× |

**geomean 35.58× → LANDS** (no size class regressed). 14.1 GB/s of output on a 39-byte string.

The truncating class is **14.03×** here against 202's 10.38×, for the same reason the main path is
cheaper: the truncating copy moves bytes rather than UTF-16 cells.

## Live substitution — PASS

`live-substitution/live_subst_iphlpapi.c` now drives **both** iphlpapi targets. 200 000 cases against
the live export — 102 828 truncating, 15 085 zero-length, 20 005 absurd — validate-first, then the
prologue hot-patched in a sacrificial single-threaded child, the counter proving our code ran 200 000
times, and the prologue restored byte-for-byte. A and W are patched and driven separately: the probe
measuring them identical is a reason to check both, not a licence to check one.
