# 226 `shlwapi!PathRemoveArgsA` — **LANDS** (34.73× geomean, up to 71.9×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.

**Bench note:** min-of-five-runs on an idle machine. Per-run geomean across five runs: 34.78 /
34.81 / 34.93 — this function's classes are all well above the timing floor, so unlike change 225
the numbers here are stable to a few parts in a thousand.

## Why this target — and why the survey understated it by twenty-fold

`discovery/shlwapi_narrow2.c` measured **69.96 ns** against **24.63 ns** for `PathRemoveArgsW` —
2.84× the wide cost for half the bytes. That is what put this function on the shortlist, and **it
is a serious understatement**, which is worth writing down because the survey looks wrong beside the
table below until you know why.

The survey times every narrow sibling on one shared subject:
`"C:\Program Files\Some Vendor\Some Product\bin\thing.exe"`. That string has a space at **byte
10**. `PathRemoveArgsA` stops at the first unquoted space, so on that subject it scans ten bytes and
returns — a shared subject is the right call for a twelve-way survey, but for *this* function it
happens to measure the earliest possible exit.

The measurement holds up exactly where it should: this benchmark's 16-byte case also splits at byte
10, and reads **72.36 ns** against the survey's 69.96. The cost is linear in the bytes actually
**scanned**, and the two points bracket the slope:

| first unquoted space at | shlwapi |
|---|---|
| byte 10 (the survey's subject shape) | 72.36 ns |
| byte 254 — i.e. a path with **no arguments at all** | 1399.53 ns |

— about **5.4 ns per byte scanned**, roughly 25 cycles a byte, which is what an MBCS-aware
per-character walk costs. And the expensive shape is the *common* one: you call `PathRemoveArgs`
precisely because you do not know whether there are arguments, so the case where there are none —
where the scan runs the whole string — is the case you pay for most often.

## The contract — three behaviours, two of them surprising

Change [175](../175-pathremoveargsw/) derived this for the wide form. It is **not** "cut at the first
space". Every rule was re-derived against the **narrow** export in `probes/pra.c`:

1. **Find the first `0x20` outside double quotes**, each `"` toggling the state.
2. **If one exists and something follows it**: NUL it, and **also NUL the *last* byte of that run of
   spaces** when a non-space follows. `"ab   c"` gets **two** terminators written, at 2 **and at 4**
   — not at 2 and 3.
3. **If there is no unquoted space**: trim **trailing** spaces, terminating at the *first* byte of
   the trailing run. This **ignores quoting entirely** — `"` + `' '` *is* cut, even though that space
   sits inside an unclosed quote, while `"` + `' '` + `'a'` is left alone.

And when there is nothing to do, **nothing is written** — not even a redundant terminator over the
existing one.

| probe | result |
|---|---|
| exhaustive `{a, ' ', '"', TAB}` to length 9 — 349 523 strings | **0 mismatches** with the wide rule |
| which bytes split | exactly one: `0x20`. A TAB does not |
| which bytes are trimmed | exactly one: `0x20` |
| all 255 non-NUL byte values at seven positions the rule consults | **0 of 255** disagree at each |
| `NULL` | returns without faulting |
| a MAX_PATH guard | **none** — lengths 250..270 all act |
| DBCS lead bytes in ACP 1252 | **0** (`GetCPInfo`, measured) |

## The quote parity is a carry-less multiply, not a state machine

Behaviour 1 looks inherently sequential: whether a space splits depends on the **parity of every `"`
before it**. It is not sequential.

The parity-of-all-preceding-bits of a bitmask is a **carry-less multiply by all-ones**. Bit *k* of
`clmul(q, ~0)` is the XOR of `q` over `[k-63, k]`, which for *k* < 64 is exactly the **inclusive
prefix XOR**. Shift that left by one for the **exclusive** prefix — a quote toggles the state of what
*follows* it, not of itself — and XOR in the carry from previous blocks:

```asm
        vmovd     xmm3, r11d
        vpclmulqdq xmm3, xmm3, xmm2, 0           ; bit k = XOR of q[0..k]: the INCLUSIVE prefix
        vmovd     ecx, xmm3
        add       ecx, ecx                       ; << 1 -> the EXCLUSIVE prefix
        xor       ecx, ebx                       ; ... and the state carried in from earlier blocks
        not       ecx                            ; ~inside
        and       r10d, ecx                      ; spaces that actually split
```

The whole 32-byte block resolves at once, and the carry for the next block is one `popcnt`. This is
the same trick JSON parsers use to find string boundaries, and it is why this function needs no
per-character state machine at all.

The terminator mask is applied to **both** the space and the quote masks before any of this, so
bytes past the end of the string cannot pollute the parity.

## Page safety

Every 32-byte load is issued only when `(cursor & 4095) <= 4064`, proving the read stays inside the
cursor's own page — necessarily mapped, since the bytes already scanned came from it. Within 32 bytes
of a page end it steps one byte and retries, **carrying the quote state by hand** (`not ebx`, which
toggles 0 ↔ −1). The backward trailing-space walk only ever moves toward the start of the string.

## Gate 1 — correctness: **PASS**

Three-way — our assembly vs an independent oracle vs the **live export on this PC** — comparing the
**whole buffer against a poison fill**, every time. That is required rather than thorough: two of the
three behaviours are invisible to a string comparison. Behaviour 2 writes a second terminator *past*
the first, so the bytes after the visible string are part of the contract; and the no-op case writes
**nothing at all**, which only poison can distinguish from writing a redundant terminator.

- every probe-derived case, including real quoted command lines
- **exhaustive** over `{a, ' ', '"', TAB}` to length 9 — **349 525** strings. The TAB is in the
  alphabet because "exactly `0x20` splits and whitespace in general does not" is a claim, and a
  corpus missing one character is precisely how eight landed changes in this repository shipped wrong
  earlier in this session
- all 255 non-NUL byte values at **seven** positions the rule consults
- **long strings with the space, the quote and the trailing run each crossing a 32-byte block
  boundary at every offset** — the quote parity is carried between blocks by a `popcnt`, and an
  off-by-one there would only show up when a quote and a space land in *different* blocks
- **dense quote masks** (every second, third, fourth and fifth byte a quote), so the carry-less
  prefix runs against something other than the one or two quotes a realistic path has
- 16 unaligned start offsets, which drives the page-check retry path and the scalar carry
- `NULL`; **300 000** randomized cases over an alphabet carrying a space, a tab and a quote
- a `PAGE_NOACCESS` guard sweep in three shapes

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 bytes, argument | 14.38 | 72.36 | 5.03× |
| 64 bytes, argument | 9.85 | 353.14 | 35.85× |
| 254 bytes, argument | 20.43 | 1469.29 | **71.92×** |
| 254 bytes, **no argument** | 20.91 | 1399.53 | 66.93× |
| 254 bytes, quoted split | 20.54 | 1418.10 | 69.04× |
| 254 bytes, trailing run | 20.62 | 1442.71 | 69.97× |
| 48-byte real command line | 14.19 | 206.31 | 14.54× |

**geomean 34.73×.** The quoted-split case costs the same as the plain one (20.54 vs 20.43 ns): the
carry-less prefix runs unconditionally, so quotes are free rather than a branch.

Note that the first three 254-byte rows are the shipped function's worst shape and the 48-byte real
command line is its best — the honest range for a caller is the whole table, not the peak. Our own
cost barely moves across all of them (14.2–20.9 ns), because a 32-byte block costs the same whatever
is in it.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_226`. This change pushes `rbx` to carry the quote parity across blocks and
has four exits — `NULL`, nothing-to-do, the trailing trim, and the two-terminator split — every one
of which has to pop it. All 8 non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced,
`DF` clear.

## Live substitution — Windows ran this code

```
[226 PathRemoveArgsA]  shlwapi (exhaustive; whole buffer against poison)
  under live patch: all match;  our-code calls = 351952
  corpus: 351952 cases -- 261402 that cut something, 53655 of those writing a
          SECOND terminator past the first, 88123 left BYTE-FOR-BYTE
          untouched (which only a poison fill can confirm), 2427 long
          enough for the quote parity to cross 32-byte blocks
  unpatched cleanly.
```

14-byte `jmp qword ptr [rip+0]` hot-patch of the real export in this process's own copy-on-write
copy, validate-first, sacrificial single-threaded child, revert verified byte-for-byte. The three
counted shapes are the point: **53 655** cases wrote a second terminator past the first and **88 123**
left the buffer byte-for-byte alone, and a string comparison would have called both of those correct
no matter what the implementation did with them.

## ISA and portability

AVX2 + BMI1 (`tzcnt`/`popcnt`) + **PCLMULQDQ**. No AVX-512, no GFNI — runs on Zen 3 and Zen 4 alike.
PCLMULQDQ is the one addition to this repository's usual baseline, and it is what makes the quote
parity a two-instruction operation instead of a loop.
