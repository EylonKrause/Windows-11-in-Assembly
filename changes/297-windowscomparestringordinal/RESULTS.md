# 297 — `combase!WindowsCompareStringOrdinal` (AVX2) — **LANDS** (3.13× geomean, 5 clean runs of 5, EVERY ROW BETTER)

- **Contract:** `HRESULT WindowsCompareStringOrdinal(HSTRING one, HSTRING two, INT32* result)`
- **Compared against:** live `combase!WindowsCompareStringOrdinal` via `GetProcAddress`.
  Windows 11 Pro 25H2 build 26200.9457.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **Correctness:** **PASS — 618,035 checks** across nine sections.
- **Gates:** ABI audit PASS; vector-re-entry audit clean; **LANDS in 5 runs of 5**, geomean
  3.082×–3.200×, and **no row in any run below 1.00×**.

## Why this one is here when its neighbours are not

The name says **ORDINAL**. `discovery/strchri_is_linguistic.c` and
`discovery/strcmpn_is_linguistic.c` ruled the `StrCmp`/`StrChrI` family *out* of this repository
precisely because those fold through the locale machinery and would need the OS collation tables to
be bit-exact.

**A name is not evidence**, so `probes/wcso.c` measured it. 400,000 random pairs over an alphabet
deliberately loaded with case pairs, ignorables, combining marks, sharp-s, U+0130/U+0131, lone and
paired surrogates, PUA and non-characters produced **zero differences** from a plain code-unit
compare — on a corpus where a *linguistic* `CompareStringW` disagrees **19.7% of the time** — and
nothing moved under `en-US`, `tr-TR`, `lt-LT`, `az-Latn-AZ`, `el-GR` or `ja-JP`.

One consequence worth stating because it is easy to assume otherwise: ordering is by **UTF-16 code
unit, not code point**, so `U+FFFF` compares **greater** than `U+10000`.

## Speed

| subject | ours ns | shipped ns | ratio |
|---|---:|---:|---:|
| 1 char (×16) | 66.10 | 115.32 | 1.74× |
| 4 chars (×16) | 67.94 | 158.24 | 2.33× |
| 16 chars (×16) | 65.57 | 167.82 | 2.56× |
| 32 chars (×16) | 72.66 | 267.40 | 3.68× |
| 64 chars (×16) | 91.03 | 371.07 | **4.08×** |
| 128 chars | 10.02 | 39.61 | 3.95× |
| 254 chars | 13.72 | 80.17 | **5.84×** |
| 1024 chars | 52.48 | 275.21 | 5.24× |
| 4000 chars | 173.97 | 1040.30 | **5.98×** |
| 254, differ at mid | 8.97 | 39.99 | 4.46× |
| 4000, differ at mid | 96.71 | 515.27 | 5.33× |
| NULL vs NULL (×16) | 28.32 | 35.53 | 1.25× |

**geomean 3.167×**, and the degenerate paths — NULL vs NULL, a one-character string — are named rows
rather than left out.

## What the shipped export actually is

A shim. It reads the two handles directly — `mov r9d,[rdx+4]` for the length, `mov r8,[rdx+10h]` for
the buffer — and forwards to `kernelbase!CompareStringOrdinal` through the
`api-ms-win-core-string-l1-1-0` IAT slot. That cross-DLL hop costs about 8 ns, which is nothing
against 83 ns at 254 characters but is most of the answer at the short sizes where this change has
its largest *ratio*.

Reading the handle fields directly is therefore what the shipped code itself does, not a guess about
an undocumented layout — and `probes/wcso.c` cross-checked `[h+4]` and `[h+0x10]` against
`WindowsGetStringLen` and `WindowsGetStringRawBuffer` over **164 live handles of every kind combase
can build**: heap, fast-pass reference, preallocated-and-promoted, and substring. `correctness.c`
re-proves it on every run.

## The contract, all of it measured

* `result == NULL` → `RoOriginateErrorW(E_INVALIDARG, 6, L"result")`, return `E_INVALIDARG`. **That
  call is not decoration**: it leaves a live `IRestrictedErrorInfo` on the thread, and
  `correctness.c` compares it **field by field**.
* `one == two` (the same handle) → `*result = 0`. It is the first test in the shipped body, and it
  covers NULL vs NULL too.
* A NULL handle is **the empty string**. NULL vs non-empty is −1, the reverse is 1.
* Otherwise: compare `min(len1, len2)` code units; on the first difference, the sign of the
  **unsigned** code-unit difference; if the common prefix is equal the **shorter string is less**.
  **Embedded NULs are ordinary characters** — the scan runs to the declared length.
* A non-NULL handle whose **buffer** is NULL → `*result = 0` against *anything*, and
  `GetLastError() == 87`. Unreachable through any documented creator; replicated anyway, because the
  gate is exact and the corpus forges the header.
* `GetLastError` is untouched on every other path.

## Correctness — 618,035 checks, nine sections

Hand-picked orderings, surrogates and linguistic-equivalents; lengths 0–80 × 16 alignments with a
difference at **every** position (+1/−1/NUL/FFFF) and every prefix length; an **embedded NUL at
every position of every length 1–40**; both buffers ending exactly at a no-access page, forged and
real fast-pass; heap, duplicate, fast-pass, promoted and substring handles plus the same-handle
early-out; length-0 and NULL-buffer handles; the NULL-result path compared on HRESULT, last error
**and** `IRestrictedErrorInfo`; and 300,000 fuzz pairs with a fixed seed over 16×16 start
alignments.

## Why no page checks are needed, proved anyway

The handle declares its length, so both buffers are guaranteed to hold `min(len1, len2)` code units
and every load lies inside that window — including the two **overlapping trailing windows**, whose
second load starts at `n−8` (resp. `n−4`, `n−2`) and is therefore still inside a string at least
that long. Section [5] proves it the hard way regardless, with both buffers ending exactly at a page
boundary whose successor is `PAGE_NOACCESS` and with no terminator at all.

## ISA and frame

AVX2 + BMI1 (`tzcnt`). **No AVX-512, so this file is portable to benches #1 and #2 unchanged.**
Nothing is pushed: the two lengths live in the **caller's shadow space**, which is ours to use, so a
four-character comparison does not pay two pushes and two pops it has no way to amortise. Only
xmm0–xmm2 are touched.
