# 264 — `ntdll!RtlInitUTF8String` — **LANDED**, 3.15–3.30× geomean (up to 7.16×), worst class 1.50×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

---

## The target

`discovery/ntdll_rtl_uncovered.c` measured it at **185.41 ns for 4000 bytes, 0.046 ns/byte** — a
strlen and a struct fill, where ntdll makes a real `call` into its own strlen.

It appeared in the same survey row as `RtlInitAnsiString`, which turned out to be **the same address
as `RtlInitString`** — change 095, landed long ago. `RtlInitUTF8String` is a *different* address, so
it is real code that this project did not cover.

## It is `RtlInitString` at a second address, and that was measured

The name says UTF-8. A function that validated its input, rejected an overlong encoding, counted
**characters** rather than bytes, or refused a lone continuation byte would produce a struct
identical to `RtlInitString`'s on ASCII and different on exactly the inputs a lazy corpus never
contains. Inheriting a rule because it looks like the same rule is how the SPACE bug got into four
landed changes at once, so `probes/equiv.c` **enumerates rather than samples**:

| | cases |
|---|---|
| every byte `0x01`…`0xFF`, and **every ordered PAIR** — every lead/continuation combination, every overlong prefix, every truncated sequence | 65 280 |
| every length from 0 to 300 | 301 |
| every length from 65 400 to 65 700, across the `USHORT` clamp | 301 |
| randomised: ASCII, high-bit, valid UTF-8, deliberately malformed UTF-8 | 60 000 |

**125 883 cases, 0 differences**, comparing all three fields against a poisoned struct. It counts
**bytes**, validates **nothing**, and clamps exactly as `RtlInitString` does — `Length` saturates at
65534 and `MaximumLength` at 65535 — and a NULL source zeroes all three fields.

## So it is an alias, not a copy

Change 095 already ships the page-safe AVX2 strlen and the struct fill this export needs, proved
bit-exact against the live `RtlInitString`. Pasting eighty lines of it here would create a second
copy that a future correction to 095 would silently leave behind — **which is precisely how change
132's extension rule ended up wrong in changes 140, 143, 144 and 158 simultaneously**.

`impl.asm` is therefore a MASM `ALIAS` directive. The export's symbol resolves to change 095's code
**in the linker**: one implementation, no duplication, and not even a jump instruction between them.

**The gates are not shared, and that is what makes this a change rather than a footnote.**
Correctness, the benchmark and the live substitution all run against the **live
`RtlInitUTF8String`** at its own address, so what is proved is that this code is bit-exact and
faster for *this* function — not that it was for a different one.

## Gate 1 — correctness: PASS

**186 384 cases, 0 mismatches**, three-way against an independent oracle and the live export,
comparing **all three fields** against a struct poisoned with `0xCD` — `Length` alone would miss an
implementation that forgot `MaximumLength`, and both would miss one that forgot `Buffer`, which the
NULL case is specifically about.

| | cases |
|---|---|
| 1. every length from 0 to 600 | 601 |
| 2. every byte and **every ordered pair** | 65 280 |
| 3. every length from 65 400 to 65 700, across the clamp | 301 |
| 4. the string **ending** at a `PAGE_NOACCESS` page, every length 0…200 and so every alignment | 201 |
| 5. NULL | 1 |
| 6. randomised: ASCII, high-bit, valid UTF-8, malformed | 120 000 |

The run fails if the clamp was never reached.

## Gate 2 — speed: PASS

Five consecutive runs: geomean **3.15×, 3.17×, 3.22×, 3.26×, 3.30×**. All 12 classes BETTER; worst
class **1.50×**.

```
size                                       ours ns   system ns    ratio   ours GB/s
4000 bytes (the survey subject)              25.58      182.03    7.12x      156.38
8192 bytes                                   51.10      366.05    7.16x      160.32
70000 bytes -- past the CLAMP               445.32     3091.41    6.94x      157.19
1000 bytes                                    7.65       52.84    6.91x      130.77
400 bytes                                     4.56       20.45    4.49x       87.74
100 bytes                                     3.37        7.18    2.13x       29.68
64 bytes (x16 calls)                         32.73       75.86    2.32x       31.29
32 bytes (x16 calls)                         35.71       56.58    1.58x       14.34
16 bytes (x16 calls)                         24.52       49.19    2.01x       10.44
8 bytes (x16 calls)                          27.58       62.10    2.25x        4.64
1 byte (x16 calls)                           24.51       39.67    1.62x        0.65
the empty string (x16 calls)                 24.41       36.50    1.50x        0.00
```

**The row past the clamp is there because it is the one length where the answer stops depending on
the string**: 70 000 bytes are scanned and 65534 is reported either way. An implementation that
stopped *scanning* at the clamp would be faster and wrong, so the subject table prints what every
row returned before any of them are timed.

The short rows are timed as sixteen calls, for the reason change 261 established by measuring it: an
empty call through this harness costs 2.32 ns, and an 8-byte Init is not much more than that.

## Gate 3 — Win64 ABI: PASS

**86 changes checked, 0 violations.** What this gate actually checks here is that change 095's code
is still ABI-clean when entered under this export's name and that the alias introduces nothing —
cheap to run, and the alternative is assuming it. Sentinels armed **per call**.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlInitUTF8String (change 264) ==
  [pre-patch]  40000 cases recorded from the SHIPPED code;  186 reached the CLAMP, 148 were NULL
  [patched]    40000 cases, 0 differ (all three fields);  our-code calls = 40000
  [post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

The corpus is built to reach both the clamp and the NULL source — the two cases an implementation is
most likely to get *partly* right — and the run counts each and fails if either is thin.

## Files

| | |
|---|---|
| `probes/contract.c` | bytes or characters, validation or none, the clamp, NULL, the return value |
| `probes/equiv.c` | the proof that this export is `RtlInitString`, over every ordered byte pair |
| `impl.asm` | the linker alias, and why it is an alias rather than a copy |
| `correctness.c` | six corpora with a guard page, all three fields against a poisoned struct |
| `bench.c` | 12 rows from the empty string to past the clamp |
| `../../live-substitution/live_subst_initu8.c` | gate 4 |
