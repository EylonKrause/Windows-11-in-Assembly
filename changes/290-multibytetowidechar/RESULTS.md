# 290 — `kernelbase!MultiByteToWideChar` (CP_UTF8, AVX2) — **PARKED** (1.82× geomean; loses every MIXED-WIDTH class)

- **Contract:** `int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr,
  int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)`
- **Compared against:** live `kernel32!MultiByteToWideChar` (a forwarder onto
  `kernelbase!MultiByteToWideChar`), resolved with `GetProcAddress`.
  `kernelbase.dll` / `ntdll.dll` **10.0.26100.9278**, Windows 11 Pro 25H2 build 26200.9457.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **Fan-in:** 211 desktop modules, 89 startup modules.
- **Gates:** correctness **PASS** against the live export; ABI audit **PASS**; vector-re-entry audit
  **clean**. Speed gate **FAILED** — see below.

> **Verdict: does NOT land, and the reason is not a defect in this change so much as an inherited
> one.** It is 5–9× on homogeneous input and **0.66×–0.94× on every mixed-width class**, and the
> gate is that no size class regresses.

## What it does well

| class | ours ns | system ns | ratio |
|---|---:|---:|---:|
| `ascii 4096` | 187.75 | 1346.15 | **7.17×** |
| `ascii 8191` | 413.90 | 2586.72 | 6.25× |
| `ascii 32000` | 1740.00 | 9743.75 | 5.60× |
| `2byte 64` | 13.47 | 74.37 | 5.52× |
| `2byte 32000` | 4139.84 | 20681.25 | 5.00× |
| `m:3byte 8191` | 1064.41 | 5467.19 | **5.14×** |
| `m:2byte 8191` | 1047.83 | 4132.81 | 3.94× |
| `z:ascii 8191` (NUL-terminated) | 486.29 | 4723.44 | **9.71×** |
| `bad 4096` (malformed) | 6971.88 | 10406.25 | 1.49× |

## What it does badly, and why

| class | ours ns | system ns | ratio |
|---|---:|---:|---:|
| `m:a+3 8191` | 6003.12 | 4671.88 | **0.78×** |
| `m:a+4 8191` | 7085.94 | 4667.19 | **0.66×** |
| `m:2+3 8191` | 6106.25 | 4857.81 | 0.80× |
| `m:1234 8191` | 7057.81 | 5204.69 | 0.74× |
| `1234 32000` | 27851.56 | 26142.19 | 0.94× |

Every regressing class is **mixed-width**: ASCII interleaved with 3-byte sequences, ASCII with
4-byte, 2-byte with 3-byte, and all four widths together. Every winning class is **homogeneous**, or
is dominated by a path that does not decode at all.

This is not a new discovery. [`discovery/utf8_width_mixtures.c`](../../discovery/utf8_width_mixtures.c)
established the same thing about change **034 `RtlUTF8ToUnicodeN`** — which is this change's engine —
and a later commit narrowed the cause: *the scalar decoder, not the probe ladder*. The vector path
handles a block only when every sequence in it is the same width; the moment widths interleave, the
block falls to a per-character scalar decode, and ntdll's own scalar decoder is a tuned one that
does not pay for the failed vector probe first.

So the honest statement is that **290 inherits 034's shape, including its weakness**, and reusing
034's encoder is exactly what made 290 tractable in the first place. It cannot be better than its
engine on the input its engine is worst at.

## Why it is kept

* It is **correct** — bit-exact against the live export across the whole corpus, including the
  malformed-input cases, whose maximal-subpart rule is the hard part and came from 034's
  reverse-engineering.
* It is **5–9× on the input the shell actually passes most of the time**, which is ASCII and
  homogeneous 2-byte.
* The dispatch boundary is proved, so it is a correct drop-in for CP_UTF8 and tail-calls the real
  export for everything else.
* It makes the cost of the mixed-width problem concrete and attributable, on a second function, at
  a size that matters.

## What would actually fix it

Not a tweak to this file. A genuinely width-agnostic vectorised UTF-8 decoder — the
`vpermb`/`vpcompressb`-style shuffle-table approach, which computes the output positions for a
64-byte block of *arbitrary* mixed widths without falling back to scalar. This machine has
`AVX512VBMI` and `AVX512VBMI2`, so it can host one; bench #1 cannot, which is part of why 034 was
never written that way.

That is a change to **034**, not to 290, and it would lift both. It is scoped out here rather than
attempted, because a half-finished decoder that is bit-exact on the cases the corpus happens to
cover is the most dangerous thing this repository could produce.

## Correctness — PASS

Against the live export via `GetProcAddress` and against `reference.c`: every length class, both
modes (convert and measure, `cchWideChar == 0`), NUL-terminated (`cbMultiByte == -1`), all four
sequence widths and all six interleavings of them, malformed input at every position including the
last byte of the buffer, over-long encodings, unpaired surrogates, and a page-guard sweep.

The measuring mode is implemented from the first commit; changes 016 and 034 shipped without theirs
and **faulted on a documented call** — see `discovery/utf8n_null_destination.c`.
