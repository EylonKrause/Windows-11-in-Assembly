# 313 — `StrToInt64ExA` + `StrToIntExA` (shlwapi, 8-bit integer parsers) — **LANDED** (8.26× geomean, 3.94×–34.1×, every row better)

- **Contract:** `BOOL StrToInt64ExA(LPCSTR pszString, STIF_FLAGS dwFlags, LONGLONG* pllRet)` and
  `BOOL StrToIntExA(LPCSTR pszString, STIF_FLAGS dwFlags, int* piRet)` — parse an optionally signed
  decimal integer, or with `STIF_SUPPORT_HEX` a `0x` hexadecimal one, from the start of the string.
- **Compared against:** live `shlwapi!StrToInt64ExA` / `StrToIntExA` via `GetProcAddress` — thunks into
  `kernelbase` (RVAs `0xF3450`, `0xF3410`). Windows 11 Pro 25H2 build **26200.8655**, ANSI code page
  **1252**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/shlwapi_str_c.c`](../../discovery/shlwapi_str_c.c), which found the wide
  parsers already at 3–12 ns, and the 8-bit ones 3–6× slower than them for the same answer. Contract
  pinned by [`discovery/strtointexa_contract.c`](../../discovery/strtointexa_contract.c) and the
  disassembly of both layers.
- **Correctness:** **PASS — 1,957,300 cases**, return value and result variable (sentinel-filled, so
  "not written" is compared too), against the live exports *and* an oracle that takes the export's own
  route — `strlen`, `MultiByteToWideChar`, the wide parse — including page-end faults, `NULL`s, strings
  past the export's stack buffer, and 160,380 through the two hand-offs, forced.

## Why

```
ns per call                        StrToInt64ExA  StrToIntExA  StrToInt64ExW  StrToIntExW
"7"                                    18.9          19.6           3.1           4.0
"1234567890"                           27.3          28.0           7.1           8.0
"  -2147483647"                        29.6          30.5           8.5           9.3
"0x7FFFFFFF" (hex)                     30.0          30.9           9.6          10.5
"9223372036854775807"                  35.3          36.4          11.1          12.8
"12 and some text after the number…"   44.8          45.7           3.6           4.5
"  -123" + 4,993 bytes of text       2601          2603
```

The 8-bit form costs **15–40 ns more** than the wide one for the same answer, and 2.6 µs for a number
at the front of a 5 KB string. kernelbase's `StrToInt64ExA`:

```
strlen(s)                                       ; the WHOLE string, not the number
n = MultiByteToWideChar(CP_ACP, 0, s, len+1, NULL, 0)   ; size it
if (n > 260) buf = LocalAlloc(LMEM_ZEROINIT, 2*(n+1))   ; else a 260-unit stack buffer (+ /GS cookie)
MultiByteToWideChar(CP_ACP, 0, s, len+1, buf, n)        ; convert it all
r = StrToInt64ExW(buf, flags, pllRet)                   ; parse a few characters of it
LocalFree(buf) if it was allocated
```

and `StrToIntExA` calls that and stores `r ? (int)ll : 0`.

## The contract

`StrToInt64ExW`'s parse (kernelbase RVA `0xF35A0`) looks only at ASCII values. On code page 1252 every
byte converts to exactly one UTF-16 unit, bytes below 0x80 to themselves and bytes above to units above
— so the conversion cannot change the answer, and the parse can run on the bytes in place:

| question | answer |
|---|---|
| leading bytes skipped | **0x09, 0x0A, 0x20 only** — not CR, VT, FF, NBSP; any number of them |
| sign | **one** `+` or `-`, immediately before the digits: `--5`, `+-5`, `- 5` are FALSE |
| hex | only with flag **bit 0** (other bits ignored: `0xFFFFFFFE` is decimal, `0x101` is hex); `0x`/`0X` after the sign; **the sign is dropped** — `-0x10` is 16 |
| digits | decimal `0-9`, hex `0-9a-fA-F`; any other byte ends the number, including every byte ≥ 0x80 (`²`, NBSP, `Á`) |
| overflow | **wraps mod 2⁶⁴**, silently and TRUE: `18446744073709551616` is 0, `99999999999999999999999` is `0x2C7E14AF67FFFFF`; the 32-bit form truncates (`2147483648` → −2147483648) |
| result | TRUE iff at least one digit was taken; `"0x"` with the flag is FALSE |
| what is written | non-NULL string: the 64-bit result **always** — 0 on failure — unless the pointer is NULL (allowed); the 32-bit result always, `(int)` of it or 0, and a NULL pointer **faults**. NULL string: FALSE, the 64-bit result untouched, the 32-bit one set to 0 (or a fault) |
| reading | **the whole string first**: `"12 xxxxx"` with no NUL before a NOACCESS page faults with nothing written, although the number ended after two bytes |

## Method

1. **The parse** ([`impl.asm`](impl.asm)) is `StrToInt64ExW`'s, transcribed onto bytes: the whitespace
   loop, one sign, the `0x` test only under flag bit 0, then a decimal loop (`10v + d` as two `lea`s)
   or a hex loop, both mod 2⁶⁴.
2. **The read.** When the parse stops on a byte that is not the NUL, a page-bounded scan continues from
   there to the NUL before anything is written — aligned 32-byte loads, which never touch a page the
   export's `strlen` would not — so an unterminated string faults exactly where the export's does, and a
   valid one never does. When the parse stops on the NUL (the common case — `"1234567890"`) there is
   nothing left to read.
3. **The writes** follow the table above, in the export's order: the result after the read.
4. **The hand-offs** ([`tables.c`](tables.c)), with `rcx`/`edx`/`r8` untouched and nothing written:
   - the ANSI code page is not single-byte, or some byte fails the one-unit / below-0x80-to-itself /
     above-to-above test, or the CPU lacks AVX2/BMI2 → every call goes to the export;
   - the string is **0x7FFFFFFE bytes or longer** → that call goes to the export, whose `(int)(len + 1)`
     conversion count stops being a length there. Neither occurs on this PC, so the gate forces both:
     the length limit lowered to 6, and the code-page flag set, over 80,000 random strings and the
     page-end cases.
   - The export's `LocalAlloc` can fail above 260 characters and make it return FALSE; this
     implementation allocates nothing and cannot. That is the one behaviour not reproduced, and it
     needs the process to be out of memory.
5. **The bench** ([`bench.c`](bench.c)): each timed op is **8 calls on the same string** through a
   monomorphic call site per (function, side) — change 304's call shape. Table ns are per 8 calls.

## Results — four runs

```
row (ns per 8 calls)                ours      system     ratio
Int64ExA "7"                        21.78     157.79     7.25x
Int64ExA 10 digits                  41.00     223.78     5.46x
Int64ExA "  -2147483647"            47.33     243.13     5.14x
Int64ExA hex 0x7FFFFFFF             57.53     246.64     4.29x
Int64ExA INT64_MAX                  71.35     293.33     4.11x
Int64ExA 12 + 52 bytes of text      34.92     364.50    10.44x
Int64ExA no number                  29.58     165.21     5.59x
Int64ExA 300 bytes                  57.23    1640.62    28.67x
Int64ExA 5000 bytes                610.84   20820.31    34.08x
IntExA "7"                          25.55     161.32     6.31x
IntExA 10 digits                    42.11     229.56     5.45x
IntExA "  -2147483647"              55.55     249.08     4.48x
IntExA hex 0x7FFFFFFF               59.78     252.45     4.22x
IntExA INT64_MAX                    74.66     297.78     3.99x
IntExA 12 + 52 bytes of text        36.25     370.22    10.21x
IntExA no number                    29.14     170.89     5.86x
IntExA 300 bytes                    57.47    1644.75    28.62x
IntExA 5000 bytes                  617.39   20834.38    33.75x
geomean                                                  8.240x
```

Geomeans over four runs: **8.240× / 8.272× / 8.259×** and a fourth with every row within 0.3 of the
first (lowest row 3.94×). Every row better in every run, and the narrowest margin is ~4×, far outside
any harness effect measured so far, so no self-control is needed.

Per call, ours is 2.7 ns for `"7"`, ~0.4 ns per decimal digit (the `lea` chain), and 77 ns for the
5,000-byte string — 0.015 ns per byte of scan. The export spends 15–40 ns on the conversion alone and
~0.5 ns per byte on long strings (twice through `MultiByteToWideChar`, plus a heap allocation above 260).

## ISA / dispatch

The parse is baseline x86-64; the NUL scan uses AVX2 and BMI2 (`shrx`, `tzcnt`), checked at init
together with the OS's YMM state; without them every call goes to the export.

**Verdict: LANDED** — 8.26× geomean, 3.94×–34.1×, every row better in all runs.
