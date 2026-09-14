# 176 `kernelbase!PathCchRemoveBackslash` — **LANDS** (1.93× geomean, up to 4.70×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `kernelbase.dll` 10.0.26100.9278.

## Why this target

The "safe" modern counterpart of `shlwapi!PathRemoveBackslashW` (change 171). Shipped cost **54 ns**
for a 254-char path — against shlwapi's 64 ns for the same work, so once again the PathCch\* form
buys an HRESULT and a bound rather than speed. (Change 143 found the modern function actually
*slower* than the one it replaces.)

## The contract (derived — `probes/pcrb.c`)

It is change 171's logic plus an HRESULT and a bounded length check:

* **The string must terminate strictly inside `cchPath`.** `"abc\"` (length 4) gives
  `E_INVALIDARG` for `cchPath` 0, 1 and 4, and `S_OK` for 5. An unterminated buffer also gives
  `E_INVALIDARG`. **The length scan must therefore be bounded** — and that is a *correctness*
  requirement, not just a safety one: reading past `cchPath` could find a terminator that does not
  count.
* **No upper bound on `cchPath` was found** — 32768, 32769 and 0x7FFFFFFF are all accepted. That is
  a genuine inconsistency inside this "safe" family: change 143's `PathCchFindExtension` rejects
  anything outside `[1, 32768]`.
* `S_OK` (0) when a backslash was removed; **`S_FALSE` (1)** when there was nothing to remove, with
  the buffer untouched; `E_INVALIDARG` is `0x80070057`.
* **Root protection is identical to change 171**, including its non-monotonic behaviour: `"C:\"`,
  `"\"` and `"\\"` are protected, while `"\\srv\"`, `"\\srv\share\"` and `"\\\"` are not.
* **The drive-letter set is identical to change 171's** — swept over all 65535 code units, exactly
  the same 114 ASCII + Latin-1 letters, **0 differences**. So the two functions, in two different
  DLLs, share one character-classification table.
* Only one backslash is removed; `/` is not a separator (`"abc/"` → `S_FALSE`).

## Method

A **bounded** AVX2 length scan — 16 characters per step, never reading past `cchPath` — then change
171's decision logic verbatim, then one 16-bit store.

### The narrow probe (what made this land)

The first cut **PARKED**, at 0.94× on the 4-character class and **0.83× on the drive-root case**.
Same cause as change 172: the buffer has just been written in narrow pieces, and a 32-byte load
cannot be satisfied from the store buffer. A two-word SWAR has-zero probe covering the first eight
characters — taken only when `cchPath` allows reading that far, since the bound is contractual —
fixed both: **0.94× → 1.06×** and **0.83× → 1.17×**.

**Page safety:** the 32-byte load is issued only when `(cursor & 4095) <= 4064` *and* at least 16
characters of budget remain, so the read is inside the cursor's page and inside the caller's
declared buffer. Within 32 bytes of a page end it tests one character and retries.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**, comparing the HRESULT *and* the whole buffer:

- exhaustive over `{a, \, :, /}` to length 6 × **every bound** from 0 to len+2
- **all 65535 first characters** with `"X:\"` — confirming the drive-letter set matches 171's exactly
- **unterminated buffers** at 64 different bounds (must give `E_INVALIDARG`)
- 16 unaligned start offsets × lengths 0..150 × trailing-backslash runs, at both the exact and a
  generous bound
- 300 000 randomized cases including the Latin-1 boundary characters
- **NOACCESS page guard** at the exact bound, with and without a trailing backslash at the edge

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 4 | 9.56 | 10.12 | 1.06× |
| 16 | 10.74 | 12.50 | 1.16× |
| 64 | 12.44 | 27.56 | 2.22× |
| 254 | 17.60 | 65.47 | 3.72× |
| 1024 | 49.41 | 232.27 | **4.70×** |
| realpath | 10.68 | 18.84 | 1.76× |
| drive-root (S_FALSE) | 6.86 | 8.03 | 1.17× |

**geomean 1.927×**

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
