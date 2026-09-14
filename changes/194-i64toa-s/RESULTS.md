# 194 `ucrtbase!_i64toa_s` — **LANDS** (2.32× geomean, up to 5.77×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The bounded form of [change 057](../057-i64toa/). **38.1 ns** for a 19-digit value, because the
shipped inner loop issues a **64-bit `div rax, rdi` per digit** — one division per output character.

## The contract — the error path had to be read, not fitted

The success path is ordinary. The ERANGE path is not: ucrtbase leaves **partial, reversed** digits in
the caller's buffer, and no rule fitted from probing explained both

* `1234`, size 4 → `0 '3' '2' '1'` (four cells used), **and**
* `-1234`, size 2 → **only `buf[0]`** touched, no digit at all.

So the shipped code was read — `dumpbin /disasm ucrtbase.dll`, RVA **0x00079D60**, whose shared
worker is at **0x00076F10**:

| condition | result |
|---|---|
| `Buffer == NULL` or `SizeInChars == 0` | EINVAL (22), **nothing written** |
| otherwise | **`Buffer[0] = 0` is written immediately**, before the rest of the validation |
| `SizeInChars <= negative + 1` | **ERANGE (34) straight away** — this is the size-2 negative case |
| `Radix` outside 2..36 | EINVAL (22), `Buffer[0] = 0` |
| otherwise | digits emitted least-significant-first into the buffer, reversed in place at the end |

That single `size <= negative + 1` test is what no fitted rule reproduced: for a negative value it
fires **before a single digit is emitted**, which is why size 2 leaves the buffer empty while size 3
leaves two reversed digits. And because digits go in reversed and are only flipped at the end, a
buffer that runs out keeps exactly the reversed prefix it managed to hold, with `Buffer[0]` then set
to 0. `errno` is set **before** the invalid-parameter handler is invoked, on both error paths.

`negative` is `Radix == 10 && Value < 0`; every other radix formats the 64-bit value **unsigned**, so
`_i64toa_s(-1, …, 16)` gives `ffffffffffffffff` — the same convention change 057 records.

## Method

Digits are generated into a stack scratch **first**, so the fit is decided before anything goes into
the caller's buffer; then the scratch is copied **forward** (success) or **backward** (the ERANGE
partial), which reproduces ucrtbase's reversed leftovers exactly without emitting twice.

| radix | path |
|---|---|
| 10 | change 057's 2-digit table — one `div` per **two** digits |
| 2, 4, 8, 16, 32 | shift and mask — **no division at all** |
| everything else | one `div` per digit, as ucrtbase does |

The 2-digit table is **assembled into the object** rather than built at run time (changes 054–057
call `wia_dec2b_init` first), so this function is self-contained and can be hot-patched over the live
export with no initialisation step.

### Two measured fixes between the first cut and this one

The first cut special-cased only radix 16 and copied the digits a byte at a time. It was already
bit-exact, and it **parked**:

| case | first cut | final |
|---|---|---|
| 64 bits base 2 | 1.00× (tie — 64 divisions) | **5.77×** |
| 19 digits base 36 | **0.92×** (regression → PARKED) | 1.19× |
| 19 digits base 10 | 1.40× | 3.10× |
| geomean | 1.298× | **2.324×** |

* **Every** power-of-two radix can shift, not just 16 — radix 2 was issuing 64 divisions.
* The byte-at-a-time copy cost ~13 cycles on a 13-digit base-36 value, which was the whole 0.92×.
  The 8-byte copy is provably in bounds: success means `negative + digits + 1 <= SizeInChars`, so
  while 8 or more digits remain there are at least 9 cells left, and the overlapping final 8 bytes
  stay inside the digit run.

## Gate 1 — correctness: **PASS**

Three-way against the oracle and the live export, comparing the return value, the **whole buffer**
(the ERANGE leftovers are part of the contract, so the tail matters), `errno`, **and** the
invalid-parameter handler hit count. Coverage: NULL buffer and size 0 × all 35 radixes; 11 invalid
radixes × 4 size shapes (including which check wins when both the radix and the size are bad); **16
values × 35 radixes × every size 0..48** — the entire ERANGE partial surface; **all 64 powers of two
× 35 radixes × every size 1..70**; 300 000 fuzz cases; and a NOACCESS page guard proving no write
passes `SizeInChars`.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| `42` base 10 | 3.54 | 4.56 | 1.29× |
| `-1234567890` base 10 | 6.35 | 15.78 | 2.49× |
| 19 digits base 10 | 13.10 | 39.62 | 3.03× |
| `_I64_MIN` base 10 | 12.90 | 38.81 | 3.01× |
| `0x7fffffffffffffff` base 16 | 8.69 | 29.76 | 3.43× |
| 64 bits base 2 | 29.75 | 171.75 | **5.77×** |
| 19 digits base 36 | 19.34 | 23.06 | 1.19× |
| 19 digits base 10, ERANGE | 31.15 | 38.68 | 1.24× |

**geomean 2.325×** (re-measured; the two runs agree to 0.001×). Base 36 is the narrowest because it
is the one class where we still divide per digit exactly as ucrtbase does — the win there is only the
lighter prologue and the wide copy. The ERANGE class is narrow for a different reason: both sides pay
a `_errno` call and a handler invocation, which dominate a 12-cell write.

## ISA and portability

Baseline x64 — no AVX needed; the work is 20 digits, not 20 kilobytes. Correct on the 5950X as well.
