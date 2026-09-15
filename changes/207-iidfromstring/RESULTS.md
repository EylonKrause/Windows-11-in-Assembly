# 207 `combase!IIDFromString` — **LANDS** (3.18× geomean, up to 4.41×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `combase.dll`. The parse half
of the COM GUID path; [change 206](../206-stringfromguid2/) is the format half.

## Why this target

32.96 ns to parse 38 characters. The disassembly at RVA `0x000E6C20` shows both reasons:

* **the length check is a one-character-at-a-time `strlen`** — 38 iterations of a two-instruction
  dependent loop, run *before* any parsing starts;
* **each hex digit costs three range compares** (`0-9`, `A-F`, `a-f`) plus a shift, an add and a store.

It costs the same whether the string parses or not — 32.96 ns valid against 33.14 ns invalid — because
that `strlen` runs either way.

## The failure contract is the hard part, and it was measured

This function **writes into the caller's GUID as it parses**, so a malformed string leaves a
*partially filled* GUID that has to be reproduced byte for byte. Rather than decode every store from
200 lines of assembly, `probes/wmask.c` asked the live function directly: corrupt exactly one
character, then report which of the sixteen output bytes moved away from a poison fill.

| corrupted char | bytes written | because |
|---|---|---|
| 0 `{` | none | the brace is checked before anything is stored |
| 1..8 | 0–3, **partial `Data1`** | `Data1` is zeroed first, then re-stored after **every** digit |
| 9 `-` | 0–3, full `Data1` | |
| 10..13 | 0–3 | `Data2` is not stored yet |
| 14 `-` | 0–3 | …not even after its four digits validate |
| 15..18 | 0–5 | `Data2` was stored once its separator validated |
| 19 `-` | 0–5 | |
| 20..21 | 0–7 | `Data3` stored after **its** separator |
| 22..23 | 0–8 | `Data4[0]` stored right after its digits — it has no trailing separator |
| 24 `-` | 0–8 | `Data4[1]` waits for the separator at 24 |
| 25..36 | one more byte per hex pair | |
| 37 `}` | **all 16** | `Data4[7]` is stored *before* the brace is checked |

So **every field is stored only once it and its trailing separator have validated** — except `Data1`,
which is progressive, and `Data4[0]`, which has no trailing separator. An implementation that parsed
into a scratch and stored on success would pass a return-value test and fail this one.

The `Data1` partial is directly visible: corrupting digit 1 leaves `Data1 = 0`, digit 2 leaves `0x0D`,
digit 3 leaves `0xDE`.

## Two error codes, not interchangeable

| | |
|---|---|
| `0x80070057` `E_INVALIDARG` | `lpiid` is NULL, or the length is not **exactly** 38. Nothing written. A structural rejection, decided before the parser runs. |
| `0x800401F4` `CO_E_IIDSTRING` | the length was right but the content is not. Partial writes as tabulated. |

The shipped code produces the second from the inner parser's boolean with
`neg eax / sbb eax,eax / not eax / and eax, 800401F4h`, which is why a *content* failure can never
return `E_INVALIDARG`. And `lpsz == NULL` is **success**: it writes the nil GUID and returns `S_OK`.

## What we do instead

* **the length check is one AVX2 pass** — two `vpcmpeqw` over chars 0..31 plus a 16-byte tail that
  checks chars 32..37 are non-NUL *and* char 38 is the NUL — so the 38-iteration walk disappears;
* **every digit is one 256-entry table lookup** instead of three range compares. `Data1` keeps a
  branch per digit because its partial value depends on *which* digit failed; every other field
  accumulates branchlessly and is tested once, since a failure anywhere in a field means the whole
  field goes unwritten. The validity accumulator puts the character's high byte in bits 8+ and the
  table's `0FFh` in bits 4..7, so neither can be mistaken for a real nibble and one
  `test acc, 0FFFFFFF0h` covers both.

**Page safety:** 39 characters plus a 16-byte tail load is 80 bytes read before the length is known.
Within 80 bytes of a page boundary the code falls back to the bounded character walk.

## Correctness — PASS

Three-way (ours vs the scalar oracle vs the **live export**), comparing the two-valued HRESULT **and
all sixteen output bytes from a poisoned baseline on every case**:

* NULL output pointer, NULL string, 7 valid forms, 10 structural rejections;
* **the corruption sweep: 38 positions × 255 byte values** — this is what pins the partial-write
  table, because each position stops the parser at a different field boundary;
* the same sweep with 12 wide characters above `0xFF`, which must not index a 256-entry table;
* double corruptions, proving the *earlier* one decides where the parser stops;
* 400 000 fuzz cases;
* a `PAGE_NOACCESS` guard with every length 0..42 ending exactly at the boundary.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| valid UPPER | 12.83 | 33.12 | 2.58× |
| valid lower | 12.89 | 31.09 | 2.41× |
| bad digit at 1 (early) | 2.74 | 12.00 | **4.39×** |
| bad digit at 36 (late) | 12.93 | 33.27 | 2.57× |
| wrong length (structural) | 2.36 | 10.40 | **4.41×** |
| mixed ×64 | 857.02 | 2840.62 | 3.31× |

**geomean 3.177× → LANDS** (no size class regressed).

The structural-rejection class is the widest win because the AVX2 length check rejects in one pass
where the shipped code still walks 38 characters first.

## Live substitution — PASS

`live-substitution/live_subst_combase.c` now drives both combase targets. 200 000 cases for 207:
**70 429** parsed, **48 093** returned `CO_E_IIDSTRING` with partial writes, and **81 478** returned
`E_INVALIDARG` — so the partial-write path ran in bulk under the patch, not just in the unit test. The
two-valued HRESULT and all sixteen bytes identical, prologue restored byte-for-byte.
