# The `itoa` family and a radix outside 2..36 — **audited, no implementation defect, two oracles fixed**

Eight landed changes reimplement this family:

`054-ultoa` `055-ui64toa` `056-itoa` `057-i64toa` `072-ultow` `073-ui64tow` `074-itow` `075-i64tow`

The radix is a **signed `int`** in all eight signatures, and every one of their corpora swept
exactly `for (int radix = 2; radix <= 36; ++radix)` and stopped. That is the shape of the defect
found in changes 097 and 100 — a parameter class the gate never asked about — so it was asked.

## The result

**No defect in any of the eight implementations.** They are **byte-identical to ucrtbase on every
out-of-range radix that returns a result**, including the high-bit garbage characters an invalid
radix produces:

| radix | ucrtbase | ours |
|---|---|---|
| 37 | `_ultoa(0xDEADBEEF)` → `"1gwec35"` | identical |
| 100 | → `7C 7A B3 AC 92 00` (`"|z³¬"`) | identical |
| 255 | → `38 A5 10 92 00` | identical |
| 256 | → `35 04 15 46 00` | identical |
| −1 | → `"F"` / `46 00` | identical |
| −2, −10, −36 | short two- and three-byte answers | identical |
| `INT_MAX` | → `"1G"` | identical |
| `INT_MIN` | → `"1F"` | identical |

That is not luck. ucrtbase converts the radix to a **32-bit unsigned** value, so `-1` becomes
4294967295 and the division proceeds normally; these implementations index the same digit table the
same way. For radix −1 and `v = 4294967295`: `4294967295 / 4294967295 = 1 remainder 0` → `"10"`.

## What DOES differ, and it is confined to termination

| radix | ucrtbase | ours |
|---|---|---|
| **0** | terminates, `0xC0000094` **integer divide by zero** | terminates, `0xC0000094` — **identical** |
| **1** | terminates, `0xC0000005` access violation | terminates, `0xC00000FD` **stack overflow** |

Radix 1 is the `v /= 1` trap — the same one that hung change 278's live harness and that every
harness in this repo now guards against. The quotient never decreases, so the digit loop never ends:
ucrtbase walks off the end of the caller's buffer, and these implementations run out of stack. Both
are unbounded loops on a caller bug, and neither is "correct". **Nothing is changed for radix 1** —
there is no right answer to reproduce, only two different ways of failing to have one.

## What the audit did change

1. **All eight corpora are widened.** The radix loop now sweeps `2..36` **and** the ten out-of-range
   values that return. Radix 0 and 1 are deliberately excluded and the comment in each file says
   why: a corpus cannot contain a case that kills the process.

2. **Two reference models were wrong, and the widened corpus caught them immediately.**
   `073-ui64tow` and `075-i64tow` converted the radix with `(unsigned long long)` rather than
   `(unsigned)`, which sign-extends `-1` to `0xFFFFFFFFFFFFFFFF`:

   ```
   FAIL v=4294967295 radix=-1:  ours '10'   sys '10'   ref 'V'
   FAIL v=4294967296 radix=-1:  ours '11'   sys '11'   ref '0'
   ```

   **`ours` matched `sys`; the oracle was the odd one out.** For radix 2..36 the two conversions are
   identical, which is exactly why a corpus that stopped at 36 could never tell them apart. Their
   siblings `055-ui64toa` and `057-i64toa` already used `(unsigned)`. Both are fixed, and all eight
   gates pass with the widened corpus.

This is worth stating plainly: the audit's finding was not in the assembly. It was in **the thing
the assembly is checked against** — and a three-way gate is what made it visible, because a
two-way check of ours-vs-Windows would have passed silently.

## Method note: one call per process

The first version of this probe put every call in one process behind `__try`. It died at **exit code
148** having printed nothing — the signature of a **fail-fast**, which `__except` cannot catch.
Changes 274 and 276 hit the same wall calling `SysFreeString` on a hand-made BSTR. The probe is now
one call per process (`radix_one.exe <ours|ucrt> <fn> <radix>`), with a driver that reads the exit
code, so a termination can be *attributed* rather than guessed at.

## Reproduce
```
audits\gate-never-asked\build.bat
```
