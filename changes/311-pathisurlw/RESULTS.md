# 311 — `PathIsURLW` + `PathIsURLA` (kernelbase, shlwapi thunks) — **LANDED** (4.60× geomean, 1.46×–17.4×, every row better)

- **Contract:** `BOOL PathIsURLW(LPCWSTR pszPath)` — "tests a given string to determine if it conforms to
  a valid URL format" — and the narrow form.
- **Compared against:** live `shlwapi!PathIsURLW` / `PathIsURLA` via `GetProcAddress` — thunks into
  `kernelbase`. Windows 11 Pro 25H2 build **26200.8655**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/shlwapi_path3.c`](../../discovery/shlwapi_path3.c) — 2.67 ns on a rooted path,
  **0.82 ns per character** on a bare name: it walks the whole name. Contract pinned by
  [`discovery/pathisurl_contract.c`](../../discovery/pathisurl_contract.c) and the disassembly.
- **Correctness:** **PASS — 1,323,862 cases** against the live exports *and* an oracle transcribed
  from their loop.

## The contract — the documentation oversells it

kernelbase's body (RVA 0x45AF0) is one loop over the start of the string:

```
if (!p) return FALSE;
for (i = 0; ; ++i) {
    c = p[i];
    if (c == 0)            return FALSE;
    if (c == ':')          return i >= 2;        // + a scheme-table lookup and a wcslen under __try,
    if (!scheme_char(c))   return FALSE;         //   both of whose results are discarded
}
```

| question | answer |
|---|---|
| `scheme_char` | exactly **`+ - . 0-9 A-Z a-z`**; nothing above 0x7F; the same at every index (a digit may lead) |
| the rule at `:` | index 0 or 1 → FALSE (`"C:\x"` is not a URL); **2 or more → TRUE**, whatever the scheme: `"zzzz:x"`, `"1ab:x"`, `"a+b-c.d:x"` are URLs |
| after the `:` | never observable: `"http:"` with a NOACCESS page right after the colon returns TRUE **without faulting** (the remainder's `wcslen` runs under `__try`) |
| an unterminated run of scheme characters into NOACCESS | faults |
| `PathIsURLA` | the same scan over bytes, **not** a conversion to UTF-16 (the colon-then-NOACCESS case does not fault there either); the same set, no high bytes |

So the cost on a bare file name — the input a path-handling caller usually has — is a byte loop
over every character of it, with a class-table lookup per character.

## Method

1. The first **four** units one at a time through a 128-byte table (`C:\` ends at index 1, `http:` at
   index 4, a UNC path at 0), each stop decided on the spot.
2. Then 16 units (W) or 32 bytes (A) per load, loaded only when the window lies inside the page the
   cursor is in; the set tested with ranges — `(c | 0x20) − 'a' < 26`, `c − '0' < 10`, and three
   compares — and the first unit outside it found with `tzcnt`. Within 32 bytes of a page end, one unit
   at a time, so an unterminated run faults on the same unit, and nothing beyond the page holding the
   colon is read.
3. [`tables.c`](tables.c) builds the table by asking the exports about every unit, and if they ever
   disagree with the range test, every call is handed to them.

## Correctness

`correctness.exe`: **PASS — 1,323,862 cases**, three-way:

1. **every unit 1..0xFFFF** (W) and every byte (A) at indices 0, 1, 2, 3, 4, 5 and 20, followed by
   `"x:"` and followed by nothing;
2. 200,000 random subjects each for W and A, 0..300 units of scheme characters with a colon somewhere
   in half of them and a foreign unit in a quarter, at every byte offset (odd ones for W);
3. **NOACCESS**: a run terminated just before it (no fault), a colon as the very last readable unit
   (no fault, same answer), and an unterminated run (both fault), every length 1..200, even and odd;
4. `NULL`.

## Benchmark

16 calls per op, each (function, side) through its own monomorphic call site (change 304's shape).
ns per call is the table's ns / 16. Three runs: **4.597×, 4.560×, 4.602×**, all LANDS. The first:

| row | ours ns /16 | export ns /16 | ratio |
|---|---|---|---|
| W rooted path (stops at 1) | 30.21 | 44.21 | 1.46x |
| W UNC path (stops at 0) | 27.33 | 47.78 | 1.75x |
| W `http://` (TRUE at 4) | 47.16 | 218.45 | 4.63x |
| W `https://` (TRUE at 5) | 47.16 | 293.51 | 6.22x |
| W `"my file.txt"` (stops at 2) | 30.89 | 66.22 | 2.14x |
| W `"readme.txt"` (to the NUL) | 47.16 | 143.80 | 3.05x |
| W 22-character name | 61.40 | 293.33 | 4.78x |
| W bare name 32 | 61.40 | 420.43 | 6.85x |
| W bare name 64 | 89.97 | 828.18 | 9.21x |
| W bare name 254 | 295.53 | 2845.31 | 9.63x |
| A rooted path (stops at 1) | 30.89 | 66.50 | 2.15x |
| A UNC path (stops at 0) | 27.33 | 59.39 | 2.17x |
| A `http://` (TRUE at 4) | 43.82 | 205.98 | 4.70x |
| A `https://` (TRUE at 5) | 43.82 | 234.70 | 5.36x |
| A `"my file.txt"` (stops at 2) | 30.88 | 77.41 | 2.51x |
| A `"readme.txt"` (to the NUL) | 43.82 | 146.86 | 3.35x |
| A 22-character name | 43.82 | 268.66 | 6.13x |
| A bare name 32 | 43.82 | 382.56 | 8.73x |
| A bare name 64 | 57.87 | 796.27 | 13.76x |
| A bare name 254 | 153.26 | 2667.97 | **17.41x** |

The `.` in a file name is a scheme character, so `"readme.txt"` is walked to its NUL by the export
too — 9 ns for ten characters.

## Reproduce

```
changes\311-pathisurlw\build.bat
```
