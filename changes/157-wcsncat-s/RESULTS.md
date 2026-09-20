# 157 — `ucrtbase!wcsncat_s` — **LANDS** (2.85× geomean, up to 6.8×)

The wide twin of [156](../156-strncat-s/), and the last of the bounded string family
(150–157). A bounded `wcslen` over `dst` followed by the same two-counter scalar loop, one wide
character per iteration: 67 ns to append 254 wide characters, 236 ns to append 16 to a
1000-character string.

## Contract
Identical to `strncat_s` in `wchar_t` units — all eight paths, including the three that differ from
`wcsncpy_s`: `count == 0` with a **NULL** src writes nothing and skips the dst walk entirely; the
same call with a **valid** src still runs the walk and can still report an unterminated destination;
and an unterminated dst writes only `dst[0]`.

## Method — the two wide-only wrinkles
**`size` is doubled up front**, with the saturating `shl / sbb / or` from [151](../151-wcscpy-s/), so
an absurd size near $2^{63}$ clamps instead of wrapping into a spurious `ERANGE`.

**`count` is deliberately *not* doubled up front**, because `_TRUNCATE` is `(size_t)-1` and doubling
would wrap the sentinel to `-2`. The `_TRUNCATE` branch is taken first, on the original value; only
then is a real `count` doubled — and a carry out of *that* shift is treated exactly like "count
exceeds the space available", which it does, since a count of $2^{63}$ or more wide characters cannot
be satisfied by any buffer. That turns an overflow check into a branch the code already needed,
rather than an extra one.

Everything else is 156 with `vpcmpeqb` → `vpcmpeqw`. Because `vpcmpeqw` sets both bytes of a matching
word, `tzcnt` lands on the low (even) byte, so **both** scans produce byte offsets and the walk, the
bound arithmetic and the copy all count bytes uniformly — which is what lets the wide version reuse
156's structure without a single unit conversion in the middle.

The two-byte `dst[0]` test before any vector load is the same store-forwarding dodge as
[152](../152-strcat-s/) and 156.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build — comparing the errno return, the handler-invocation
count and **every byte** of a canary-filled destination, over **16 src alignments × 8 dst alignments
× dst prefix 0..40 × src length 0..40 × counts within ±2 of the source length × 6 size bounds each**,
plus `_TRUNCATE` at six sizes; all six NULL/size-0/count-0 validation paths; the unterminated-dst
path over every `size ∈ 1..140` for both a counted and a `_TRUNCATE` call; a **src NOACCESS
page-guard sweep**; and a **dst page-guard sweep** over every `size ∈ 1..140` × 3 count shapes.

The destination prefix uses a **zero-low-byte** family (`0x4100…`) and the source alternates
zero-low-byte and zero-high-byte families — a byte-granular walk or scan would find a false
terminator in either.

## Benchmark — vs live `ucrtbase!wcsncat_s`
geomean **2.85×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| append 7 | 5.25 | 6.82 | 1.30x |
| append 15 | 5.56 | 7.56 | 1.36x |
| append 31 | 6.45 | 11.12 | 1.72x |
| append 63 | 8.23 | 26.44 | 3.21x |
| append 254 | 18.91 | 66.77 | 3.53x |
| append 1024 | 62.95 | 237.96 | 3.78x |
| append 4096 | 247.57 | 927.26 | 3.75x |
| append 16 to a 1000-wchar dst | 34.71 | 236.19 | **6.80x** |
| append 254, `_TRUNCATE` truncating | 10.23 | 38.00 | 3.71x |

## Reproduce
```
changes\157-wcsncat-s\build.bat
```

## Correction — a NULL source with `count == 0` still validates the destination (2026-09-20)

This implementation's rule 3 read *"count == 0 AND src == NULL -> return 0, NOTHING WRITTEN and no
handler"*. That is right only when the destination is **already a valid string within `size`**.

`wcsncat_s(L"A", 1, NULL, 0)` — no terminator in `dst[0..size)` — returns **EINVAL**, sets `dst[0] = 0`
and calls the invalid-parameter handler. This returned 0, in silence, leaving the destination alone.

[`probes/ncat0.c`](probes/ncat0.c) drives the whole small grid and the rule is exact: with
`count == 0` and a NULL source the answer is 0 **only** when `size != 0` and a terminator lies
within `dst[0..size)`. `dst = L"A"` with `size >= 2` returns 0 and leaves the string alone;
`dst = ""` with `size >= 1` returns 0; `size == 0` is EINVAL either way.

Found by [`live-substitution/live_subst_secure.c`](../../live-substitution/live_subst_secure.c) on
**3 of 16000 cases**, where the return code, the destination byte **and the invalid-parameter
handler count** all disagreed. The handler count is only observable because that harness installs
`_set_invalid_parameter_handler` — without it the first NULL argument would terminate the process
instead of being measured.

Correctness still PASSES and the change still LANDS; the harness that found it now reports **0 of
16000**.

