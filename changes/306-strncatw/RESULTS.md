# 306 — `StrNCatW` (shlwapi, bounded wide append) — **LANDED** (3.28× geomean, 1.03×–8.4×, no row worse)

- **Contract:** `PWSTR StrNCatW(PWSTR psz1, PCWSTR psz2, int cchMax)` — append at most `cchMax − 1`
  characters of `psz2` to `psz1` and terminate.
- **Compared against:** live `shlwapi!StrNCatW` via `GetProcAddress` — implemented in `shlwapi` itself.
  Windows 11 Pro 25H2 build **26200.8655** (`shlwapi.dll` 10.0.26100.8521).
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/shlwapi_str_c.c`](../../discovery/shlwapi_str_c.c); contract pinned by
  [`discovery/strncatw_contract.c`](../../discovery/strncatw_contract.c) and the export's disassembly.
- **Correctness:** **PASS — 599,786 cases**, comparing the whole destination arena against the live
  export *and* an oracle transcribed from the disassembly.

## Why

```
StrNCatW    16 + 16    15.56 ns      256 + 256    186.72 ns      4096 + 4096    2796.09 ns   0.341 ns/char
  wcscat               9.88 ns                    138.77 ns                     2021.88 ns
```

A scalar scan to the end of `psz1`, then a scalar copy: one character at a time, twice.

## The contract — read from the code, then checked

`StrNCatW` is twenty instructions: `NULL` checks, a scalar scan to the end of `psz1`, and a call to a
`StringCchCopyW`-style worker with `cchMax` sign-extended. The worker, disassembled:

```
if (n - 1 > 0x7FFFFFFE)               // n <= 0
    { if (n != 0) *p = 0; return E_INVALIDARG; }
while (n && src[i])  { p[i] = src[i]; ++i; --n; }      // and a STRSAFE_MAX_CCH guard
if (n == 0) p[i - 1] = 0; else p[i] = 0;               // truncation overwrites the last copied char
```

`strncatw_contract.c` confirmed each consequence on the live export:

| question | answer |
|---|---|
| return value | `psz1`, always |
| `psz1 == NULL` | returns `NULL`, no fault; `psz2 == NULL` returns `psz1` untouched — neither is scanned |
| characters appended | `min(cchMax − 1, wcslen(psz2))`, then a NUL; `"abc" + "defgh"`: n=2 → `"abcd"`, n=6 → `"abcdefgh"` |
| `cchMax ≤ 0` | nothing appended; **`cchMax < 0` re-writes the NUL at the end** (a fault on a read-only buffer), `cchMax == 0` writes nothing |
| an unterminated `psz1` | faults **with nothing written** — the end is found first |
| an unterminated `psz2` | faults after writing **every readable character** (1/1, 3/3, 17/17, 40/40) |
| what is read | `psz2[0 .. cchMax − 1]`, **including** index `cchMax − 1` — the worker copies it and then overwrites it with the NUL. So 5 readable characters and n = 5 return normally, while n = 6 faults after writing all 5 and **no** NUL |
| `psz2` inside `psz1`'s tail | a forward character loop re-reads what it wrote: `StrNCatW("abcdef", d + 2, 10)` gives `"abcdefcdefcdefc"` |

## Method

1. `NULL` guards; the end of `psz1` found **before** anything is written: the first eight characters
   one at a time, then 303's page-bounded 32-byte `vpcmpeqw` scan.
2. `cchMax ≤ 0`: the worker's error path, including the NUL re-write for `cchMax < 0`.
3. **The overlap rule.** The block copy below reproduces the forward character loop exactly whenever the
   write cursor `p` is below `psz2` or at least 32 bytes above it — no block can then read a byte its
   own stores change, and every byte an earlier block changed was changed before the character loop
   would have read it. For `0 < p − psz2 < 32` the worker's own character loop runs instead.
4. **`psz2[0]` alone:** an empty source and `cchMax == 1` end here, as the worker does.
5. **The first four characters as one qword**, when it lies inside `psz2`'s page (so it cannot fault):
   the first NUL by the 16-bit has-zero trick, `m = min(t, cchMax − 1)` characters kept, written with
   one to two stores from `bzhi(q, 16·m)` — whose high lanes are the NUL. Nothing is stored unless the
   copy ends here.
6. **Change 303's copy core with a count**, from the original pointers: a 32-byte block is loaded only
   when it lies inside `psz2`'s page and in full before any of it is stored, and within 32 bytes of a
   page end the copy steps one character at a time — so the fault point and the written prefix are the
   export's. A NUL inside the count ends it with a two-load/two-store tail; the count ending first copies
   `cchMax − 1` characters and writes the NUL at `p[cchMax − 1]`.

One divergence, unreachable: the worker's `STRSAFE_MAX_CCH` guard stops at 0x7FFFFFFE characters, so for
`cchMax = 0x7FFFFFFF` and a source of 2³¹ characters without a NUL — 4 GiB — it does not read the
last index. This does.

### What it took

| step | what happened |
|---|---|
| first build | correct on the first attempt, but **every short row ~9.5 ns** — `0 + 1` at 0.34×. The scan's first 32-byte load spanned the terminator the bench had just restored and the previous call's narrow stores: no store-to-load forwarding, a wait for them to drain |
| scalar lead-in for the scan | `0 + 1` → 1.00×, `0 + 16` → 2.6×; rows whose destination reaches the vector scan still stall |
| the restore on a rotated destination | the bench's own restore store was charged to us — change 230's diagnosis exactly. The bench now rotates four identical destinations and prints both shapes (below) |
| `1 + 1, n = 1` at 0.70× | vector setup for a one-character copy. A character-at-a-time head fixed it — and **cost the 4096 row 18%**: advancing both cursors by 8 bytes misaligned every later block (303's lesson again). Replaced by the read-only qword head plus the `psz2[0]` check: 1.28× and the long rows back |
| the block loop moved with every edit | 5908 → 6817 ns on `4096 + 4096` from six added instructions in front of it. The loop is now 64-byte aligned behind a `ret`, so the padding never runs: 5384 ns |

## Correctness

`correctness.exe`: **PASS — 599,786 cases**, the return value and the whole poisoned arena, three-way:

1. `psz1` lengths 0..40 × `psz2` lengths 0..80 × **every** `cchMax` from −3 to `len + 3` × even and odd
   offsets of both;
2. 60,000 random cases up to 300 characters each, `cchMax` from −50 to 350;
3. **overlap**: `psz2` inside `psz1`'s string with the write cursor 1..60 characters above it, at even and
   odd byte distances, every `cchMax` up to 90 — and `psz1` below `psz2` in the same buffer;
4. the `psz2` NUL as the last wchar before a **NOACCESS** page, every length 0..300, even and odd, five
   values of `cchMax` around the length;
5. an **unterminated** `psz2` into NOACCESS, lengths 1..200: with `cchMax` large, both fault and the
   destinations are byte-identical; with `psz2[cchMax − 1]` the first unreadable unit both fault after
   `cchMax − 1` characters and no NUL; with it the last readable unit neither faults;
6. an unterminated `psz1`: both fault, nothing written;
7. `NULL`s, and `cchMax` ∈ {−1, −5, 0} on a **read-only** destination: the export faults for the negative
   values (it re-writes the NUL) and not for 0 — so does this;
8. a 1M-character append, whole and truncated.

## Benchmark

16 calls per op, each side through its own monomorphic call site (change 304's shape), the restore on a
rotated destination (change 230's). ns per call is the table's ns / 16. Four consecutive runs: **3.365×,
3.290×, 3.296×, 3.282×**, all LANDS, no `WORSE` row in any. The run below:

| row (`dlen + slen`, `cchMax` 100 unless shown) | ours ns /16 | shlwapi ns /16 | ratio |
|---|---|---|---|
| 0 + 1 | 54.21 | 55.77 | 1.03x |
| 0 + 16 | 83.64 | 175.99 | 2.10x |
| 8 + 8 | 84.24 | 137.09 | 1.63x |
| 16 + 16 | 101.44 | 232.88 | 2.30x |
| 16 + 16, n=8 | 90.85 | 158.66 | 1.75x |
| 16 + 1, n=2 | 79.87 | 119.56 | 1.50x |
| 40 + 1 | 89.88 | 211.76 | 2.36x |
| 1 + 1, n=1 | 38.00 | 48.66 | 1.28x |
| 64 + 64 (n=1000) | 151.29 | 815.89 | 5.39x |
| 256 + 256 (n=1000) | 379.05 | 2999.22 | 7.91x |
| 256 + 256, n=100 | 268.75 | 1867.81 | 6.95x |
| 0 + 1024 (n=2000) | 877.51 | 7404.69 | **8.44x** |
| 1024 + 1024 (n=4096) | 1586.17 | 11190.62 | 7.06x |
| 4096 + 4096 (n=10000) | 5384.38 | 44020.31 | 8.18x |
| 4096 + 8 | 2476.56 | 14821.88 | 5.98x |

### The cost that remains: appending to a destination written a moment ago

Every run also prints the **same-buffer** shape — the restore on the buffer the call is about to scan.
From the same run:

```
                   same buffer                          rotated
0 + 1              ours  50.89 live  51.55 -> 1.01x     ours  60.58 live  76.34 -> 1.26x
8 + 8              ours 172.87 live 132.89 -> 0.77x     ours  83.99 live 137.20 -> 1.63x
16 + 16            ours 149.07 live 228.45 -> 1.53x     ours 101.44 live 232.89 -> 2.30x
16 + 16, n=8       ours 171.75 live 154.35 -> 0.90x     ours  91.10 live 158.66 -> 1.74x
16 + 1, n=2        ours 156.79 live 111.63 -> 0.71x     ours  72.07 live 116.45 -> 1.62x
40 + 1             ours 158.64 live 207.33 -> 1.31x     ours  87.69 live 211.74 -> 2.41x
1 + 1, n=1         ours  37.77 live  44.53 -> 1.18x     ours  38.00 live  48.66 -> 1.28x
64 + 64            ours 190.59 live 811.44 -> 4.26x     ours 151.24 live 832.64 -> 5.51x
```

The gate follows change 230's precedent and measures the rotated shape, but the same-buffer rows are
not only a bench artefact. A destination of **8 or more characters whose end was written within the
last few dozen cycles** — the previous append's NUL, say — costs this implementation a store-to-load
forwarding stall of about 5 ns that the export's word-at-a-time scan does not pay, and a short append
onto it is then **0.71×–0.90×**. From a 40-character destination, or a 16-character append, on, the
vector work pays for the stall even in that shape. The scalar lead-in moves the threshold; it cannot
remove it without giving up the vector scan.

## Reproduce

```
changes\306-strncatw\build.bat
```
