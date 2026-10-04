# 303 — `StrCpyW` + `StrCatW` (shlwapi) — **LANDED** (4.27× geomean, 1.07×–12.47×, every row better)

- **Contract:** `PWSTR StrCpyW(PWSTR psz1, PCWSTR psz2)` copies `psz2`, terminator included, to `psz1` and
  returns `psz1`; `PWSTR StrCatW(PWSTR psz1, PCWSTR psz2)` appends `psz2` at the end of `psz1` and returns
  `psz1`.
- **Compared against:** live `shlwapi!StrCpyW` / `StrCatW` via `GetProcAddress`. Windows 11 Pro 25H2 build
  **26200.8655** (`shlwapi.dll` 10.0.26100.8521).
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/shlwapi_str_c.c`](../../discovery/shlwapi_str_c.c); contract pinned by
  [`discovery/strcpyw_contract.c`](../../discovery/strcpyw_contract.c).
- **Correctness:** **PASS — 397,775 cases** against the live exports *and* a scalar oracle.

## Why

The shlwapi `Str*` exports left unconverted after changes 131–239 split into the linguistic comparisons
(ruled out by `strcmpn_is_linguistic.c`) and a remainder nobody had timed. `shlwapi_str_c.c` timed the
remainder next to the C runtime doing the same job:

```
                 16 chars        256 chars        4096 chars
StrCpyW          8.44 ns         119.34 ns        1825.78 ns   0.446 ns/char
  wcscpy         5.78 ns          14.01 ns         186.91 ns   0.046 ns/char
StrCatW 16+16   13.34 ns   256+256 185.16 ns  4096+4096 2794.92 ns
  wcscat         9.88 ns         138.77 ns        2021.88 ns
```

`StrCpyW` moves one character per iteration — **0.446 ns per character, 9.7× the CRT** at 4096. It is a
loop, not a workload.

## The contract, measured rather than assumed

A block copy is easy; a block copy that is the **same function** is not, because a scalar copy has
observable behaviour on bad input that a block copy changes. `strcpyw_contract.c` measured it first:

| question | answer |
|---|---|
| return value | the destination, always |
| `NULL` destination, or `NULL` source | **no fault**; returns the destination unchanged (so `NULL` for a `NULL` destination) |
| an **unterminated** source running into a NOACCESS page | **faults after writing every character it could read** — 3 of 3, 17 of 17, 40 of 40 |
| a destination overlapping just **below** the source (`dst = src − g`) | the result is **identical to `memmove`** for every gap tried |
| `StrCatW` with an unterminated **destination** | **faults with the destination unchanged** — it finds the end before it writes |

The third row decides the design. A block copy that loads ahead of the cursor faults *earlier* than the
export and leaves *fewer* characters written: a different function on exactly that input.

## Method

1. **`NULL` guards** on both arguments, returning the destination.
2. **One- and zero-character strings exit first.** When the first 4 bytes lie inside the source's page,
   they are read as one dword: a zero low word is the empty string, a zero high word is one character
   plus its terminator — stored as one dword, returned. Anything longer leaves this head having written
   **nothing**, and the copy restarts from the original pointer.
3. **32-byte blocks, page-bounded.** A block is loaded only when all 32 bytes lie inside the page the
   source cursor is in, so a load can never fault. Within 32 bytes of a page end the copy steps one
   character at a time, exactly as the export does — so when the next page is unreadable, the fault lands
   on the same character with the same prefix already written.
4. **The terminating block** is copied with two overlapping loads (16, 8, 4 or 2 bytes each) taken
   **before** either store. Every full block is likewise loaded completely before it is stored, so no
   store can reach source bytes that have not been read yet — which is exactly what makes a destination
   below the source come out as `memmove` would leave it.
5. **`StrCatW`** finds the destination's end first with the same page-bounded `vpcmpeqw` scan, writing
   nothing, then jumps into the copy. An unterminated destination therefore faults unchanged.

Loads are unaligned and relative to the cursor, so an odd source pointer needs no special case: the
16-bit lanes are its characters.

A destination **above** the source and overlapping it is not reproduced: the export overwrites the
source's own terminator and runs until it faults. That is out of contract, as it is for `wcscpy`.

### The head, and what it cost to get it right

The first benchmark parked the change on one row: **1 character, 0.83×** (4.00 ns against 3.33). The
export's loop runs twice there; the vector path paid for its setup and a `vzeroupper`.

A first fix copied the first **four** characters as one scalar qword and continued from there. It did
not move the 1-character row at all — and it **halved the 256-character row** (12.68 → 23.57 ns), because
every later 32-byte load now started 8 bytes past the source's alignment and split a cache line. The
head had to stay out of the loop's way: it now stores **only** when it finishes the string (0 or 1
characters), and otherwise writes nothing and lets the loop start from the original pointer. Result:
1 character **3.11 ns (1.07×)**, 256 characters back to **12.23 ns**. Three consecutive runs gave the
same numbers to the hundredth of a nanosecond.

## Correctness

`correctness.exe`: **PASS — 397,775 cases**, comparing return values and the **whole arena** around each
destination, so a byte written before it or past its terminator fails as loudly as a wrong character:

1. `StrCpyW`: lengths 0..260 × source byte offsets 0..33 × destination byte offsets 0..33, **odd ones
   included**, against both the export and the oracle;
2. `StrCatW`: destination lengths 0..80 × source lengths 0..80 × offsets;
3. the source terminator as the last wchar before a **NOACCESS** page, at every length 0..300, even and
   odd placement — one byte of over-read and this faults;
4. an **unterminated** source running into NOACCESS, lengths 1..200, even and odd: both must fault, and
   the destinations must be **byte-identical** afterwards;
5. an unterminated `StrCatW` destination: both fault, nothing written;
6. a destination overlapping just below the source, every gap 1..40 characters × lengths 0..200 × two
   alignments, against the export;
7. `NULL` destination / `NULL` source, both functions;
8. a 1M-character string.

## Benchmark

geomean **4.27×**, every row better:

| row | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| cpy 1 | 3.11 | 3.33 | 1.07x |
| cpy 8 | 4.67 | 6.44 | 1.38x |
| cpy 16 | 5.56 | 10.00 | 1.80x |
| cpy 64 | 6.89 | 32.00 | 4.64x |
| cpy 256 | 12.23 | 120.86 | 9.88x |
| cpy 1024 | 37.55 | 462.19 | 12.31x |
| cpy 4096 | 146.52 | 1827.44 | **12.47x** |
| cat 16+16 | 10.89 | 13.11 | 1.20x |
| cat 256+256 | 19.58 | 183.30 | 9.36x |
| cat 4096+4096 | 288.08 | 2742.97 | 9.52x |

The short rows are thin by nature — the export's 1-character copy is two iterations of a tight loop,
and the bench harness's own floor is near 3 ns — but they are stable across runs, and from 64
characters on the block copy is 4.6×–12.5×.

## Reproduce

```
changes\303-strcpyw\build.bat
```
