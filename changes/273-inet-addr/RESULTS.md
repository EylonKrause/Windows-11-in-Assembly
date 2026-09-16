# 273 — `inet_addr` — **LANDED** (ws2_32, 3.51× geomean)

The lenient IPv4 parser. `discovery/sid_inet_bstr.c` measured it at **24.98 ns** and flagged it as the
one that takes `"1.2"`, `"0x7f.1"` and octal — all of which `RtlIpv4StringToAddressA` (change 114)
refuses.

- **Contract:** `unsigned long inet_addr(const char* s)`, returning the address in **network** byte
  order, or `INADDR_NONE`.
- **Compared against:** live `ws2_32.dll!inet_addr`. **ISA:** baseline x64. There is nothing to
  vectorise in a four-number parse; what makes it fast is that every character costs **one table
  load** which answers "is this a digit in this base", "what is it worth" and "is it whitespace" at
  once.
- Works before `WSAStartup`, which `probes/grammar.c` checked first because refusing before startup
  would be a behaviour of its own.

## It corrects a claim this repository already had

`live-substitution/live_subst_ws2.c` opened with **"ws2_32's IP-conversion exports are ALREADY
COVERED by landed ntdll changes"** and **"ws2_32 does not parse or format an IP address at all"**. It
patches `ntdll!RtlIpv4StringToAddressA`, calls `ws2_32!inet_addr`, and watches a counter move on all
22 of its subjects.

The counter is right: `inet_addr` **does** call that export, on every input. The conclusion is not.
`probes/grammar.c` asks the two functions the same ten questions:

| | `inet_addr` | `RtlIpv4StringToAddressA` |
|---|---|---|
| `"1.2"` | `02000001` | refuses |
| `"1"` | `01000000` | refuses |
| `"0x7f.1"` | `0100007F` | refuses |
| `"010.1.1.1"` | `01010108` | refuses |
| `"1.2.3.4x"` | refuses | `04030201` |

`inet_addr` calls the ntdll export and then, when it refuses, **parses the string itself** with a far
more permissive grammar. Change 114 is on its path but is not its answer.

**That harness could never have caught it**, and the reason is worth keeping: patching change 114
with a *bit-exact* replacement leaves the composite answer unchanged whichever branch `inet_addr`
takes afterwards. A harness that only asks "did the answer change" cannot tell which of two
implementations produced it. `live_subst_ws2.c` now says so, and this change patches
`ws2_32!inet_addr` itself.

## Three rules that are not what the source everyone quotes does

### 1 — The overflow test is "did the accumulator go down" (`probes/accum.c`)

The accumulator is 32 bits and **wraps**; a digit is refused only when the wrapped result is
**strictly less** than the value before it.

| | |
|---|---|
| `0x112345678` | accepted, `0x12345678` — **the textbook check `acc > (MAX-d)/16` refuses this** |
| `0x212345678` | refused — `0x12345678 < 0x21234567`, so it went down |
| `0x7FFFFFFF0` | accepted, `0xFFFFFFF0` — overflows 32 bits and is accepted anyway |
| `12345678901` | accepted, `0xDFDC1C35` — wrapped, and larger than `1234567890` |
| `99999999999` | refused — wrapped to something smaller |

`probes/accum.c` swept all sixteen leading hexadecimal digits at nine and ten digits: **accepted if
and only if the leading digit is 0 or 1**, which is what this test predicts and no other does. It is
one instruction — `cmp edx, eax / jb fail` — and an implementation with the *correct* overflow check
is wrong on inputs anyone could type. The mutation test below replaces it with the textbook check
and the gate catches it.

**The probe that found this had to be written twice.** `probes/bytes.c` asked what `0x` plus *k*
digits returns and built the digits out of **ones** — and `0x11111111` has all four bytes equal, so
the answer reads the same whether the parser wrapped, truncated, saturated or byte-swapped
(`inet_addr` returns network order). It was a test whose input could not distinguish the hypotheses.
`probes/accum.c` asked again with four different bytes.

### 2 — Whitespace ends the address and the rest is never looked at (`probes/bytes.c`)

Any of `09 0A 0B 0C 0D 20`, once a digit has been consumed, ends the parse — and **the rest of the
string is never examined**: `"1.2.3.4 junk"` is 1.2.3.4, `"1 junk"` is 0.0.0.1, `"1.22.33 .44"` is
1.22.0.33. Leading whitespace is refused. That set came from sweeping every byte in twelve positions,
not from `isspace`: the sweep found `1590` accepted two-byte trailers out of 65,025, which is exactly
"six whitespace bytes × everything".

### 3 — The single byte `0x20` is an address (`probes/lonespace.c`)

`" "` — one space and the terminator, nothing else — comes back as **0.0.0.0**. Two spaces do not. A
tab does not. `" 1"` does not. `""` does not. The probe asked from every side — how many spaces, what
follows them, each of the six whitespace bytes alone, and each alone after a digit — and **no model
of the grammar explains it**. It is one input out of all possible inputs, and it is reproduced
because bit-exactness is the standard and a gate comparing against the live export would otherwise
report it forever.

### And one ambiguity that is built in

`INADDR_NONE` is `0xFFFFFFFF`, which is also the value of `255.255.255.255` — so a refusal and that
one address are indistinguishable, here, in the live export, and to every caller.
`probes/grammar.c` confirmed the last error is not set either way.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**. There is
only one thing to compare — the 32-bit result — so the whole weight of this gate is the corpus.

| corpus | cases |
|---|---:|
| 0. the assembler-generated class table against its definition | — |
| 1. every byte `0x01`–`0xFF` in 17 positions | 4,335 |
| 2. 9–24 digits, **every leading digit**, three bases, three positions | 2,304 |
| 3. every field boundary, three bases, seven forms | 462 |
| 4. six whitespace bytes at every position of six addresses, with and without trailing junk | 456 |
| 5. up to 300 leading zeros, decimal and hexadecimal | 129 |
| 6. every length 0–200 ending exactly at a **guard page** | 201 |
| 7. randomised over the grammar's alphabet | 300,000 |
| 8. the NULL argument | 1 |
| | **307,888** |

**0 mismatches**, on the first run. The live export accepted 16,385 and refused 291,502.

**Mutation-tested, 8 mutants, all 8 caught** by both gates: the overflow test dropped; the overflow
test also refusing when equal (which breaks leading zeros); **the overflow test replaced by the
textbook range check**; whitespace no longer ending the address; the one-byte space no longer an
address; a part allowed to start with a hexadecimal letter; the three-part tail given 24 bits instead
of 16; and `"0x"` with no digit accepted.

## ABI — PASS

`tools\abi-check\check.bat 273`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. A leaf with a frame and **no calls** — the shape whose unwind data nobody checks because
nothing ever unwinds through it, until something does. The thunk drives all four forms, all three
bases, the wrapping accumulator, the whitespace terminator, the one-byte special case, five refusals
and NULL, with sentinels armed **per call**.

## Live substitution — PASS

`live-substitution\build_inetaddr_live.bat` patches **`ws2_32!inet_addr` itself**, 40,000 cases:

```
[pre-patch]  40000 cases;  accepted 14630, refused 25370;  5714 took the WRAPPING
             ACCUMULATOR class (9..20 digits) and 5714 the WHITESPACE TERMINATOR class
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

The corpus is built from what the probes found, not from plausible dotted quads — which exercise
none of it — and the harness fails if either of those two classes comes back thin.

## Speed — LANDS (no size class regressed)

Thirteen rows, ×8 calls each (the shipped export is ~25 ns, close enough to the 2.32 ns harness floor
that change 261's `probes/floor.c` measured that a single call would be dominated by it).

| row | ours ns (×8) | ws2_32 ns (×8) | ratio |
|---|---:|---:|---:|
| `1.2.3.4` | 55.80 | 158.34 | 2.84× |
| `192.168.100.200` | 80.55 | 297.40 | 3.69× |
| `255.255.255.254` | 81.34 | 300.00 | 3.69× |
| `10.0.0.1` | 52.65 | 171.84 | 3.26× |
| three parts | 46.36 | 124.92 | 2.69× |
| two parts | 33.79 | 89.37 | 2.64× |
| one part, decimal | 46.36 | 170.99 | 3.69× |
| one part, hexadecimal | 47.92 | 232.81 | 4.86× |
| one part, octal | 58.84 | 239.46 | 4.07× |
| wrapping accumulator | 50.98 | 206.24 | 4.05× |
| whitespace then junk | 54.48 | 240.26 | 4.41× |
| a refusal, late | 58.94 | 191.54 | 3.25× |
| a refusal, immediate | 14.08 | 45.37 | 3.22× |

**Overall geomean 3.509× over 13 rows. Worst row 2.64×. No size class regressed → LANDS.**

Part of the win is structural rather than instruction-level: the shipped export calls
`RtlIpv4StringToAddressA` first and only parses the string itself when that refuses, so every lenient
form is paid for twice.

## Reproduce
```
changes\273-inet-addr\build.bat
tools\abi-check\check.bat 273
live-substitution\build_inetaddr_live.bat
```
and the probes the contract was read from, in the order they were needed:
```
cl /O2 /EHa probes\grammar.c   ws2_32.lib            & grammar.exe
cl /O2      probes\bytes.c     ws2_32.lib user32.lib & bytes.exe
cl /O2      probes\overflow.c  ws2_32.lib user32.lib & overflow.exe
cl /O2      probes\accum.c     ws2_32.lib user32.lib & accum.exe
cl /O2      probes\lonespace.c ws2_32.lib user32.lib & lonespace.exe
```
