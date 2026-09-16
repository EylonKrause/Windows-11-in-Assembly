# 250 — `RtlIpv6StringToAddressExW`

`ntdll!RtlIpv6StringToAddressExW` — **the last missing member of a sixteen-function family.**
**LANDED — 2.46–2.50× geomean over five runs, up to 4.96×, worst size class 1.14×.**

---

## Why it was missing, and why that reason was wrong

Ipv4/Ipv6 × StringToAddress/AddressToString × A/W/ExA/ExW is sixteen exports. `image/tree` carried
fifteen. The gap was this one, and it was left on purpose: change 122 landed
`RtlIpv6StringToAddressExA` and its README row says, in as many words —

> `ExW` scoped out — Unicode digits

Change 166 then landed `RtlIpv6StringToAddressW` and settled what the wide **address** parser folds:
exactly seventeen contiguous blocks of ten, the frozen Unicode 3.0 `Nd` list. The natural reading was
that `ExW` would need that table again for its scope and its port.

**`probes/ip6exw.c` sweeps all 65 536 UTF-16 units through both positions and the reading is false:**

| position | units accepted | ≥ 0x80 | run starts |
|---|---|---|---|
| scope, `[::1%<u>]` | 10 | **0** | `0030` |
| port, decimal | 10 | **0** | `0030` |
| port, octal | 10 | **0** | `0030 0058 0078` |
| port, hex | 22 | **0** | `0030 0041 0061` |

**The envelope is pure ASCII.** Not one non-ASCII unit is a digit in either position, at any base, and
U+0661, U+0665, U+FF10 and U+FF11 are all refused outright in both. The seventeen blocks live **only**
in the address body — and the address body here *is* change 166's export.

(The "octal" row's three runs are `'0'`–`'7'` plus `'X'` and `'x'`, which are not octal digits at all:
they turn `0<u>` into a `0x` prefix with an empty body, which the shipped parser accepts as port 0.)

So the blocker was never real, and the function is an envelope over a landed change.

## The composition is literal

```
ntdll!RtlIpv6StringToAddressExW, RVA 0xC3120:
  000C314C..0C316A  four NULL checks -> STATUS_INVALID_PARAMETER
  000C3177  cmp bp, 0x5b            a leading '[', remembered
  000C318E  call 0x0C33F0           <== AND 0x0C33F0 IS RtlIpv6StringToAddressW's OWN RVA
  000C31A6  cmp word ptr [rdi],0x25 '%' -> the scope
  000C31B4  cmp bx, 0x80 / jae err  <== the scope's ASCII gate, in the binary
  000C31E3  cmp ax, 0x5d            ']'
  000C31FE  cmp ax, 0x3a            ':' -> the port
  000C3211..0C323A                  "0x"/"0X" -> 16, a leading '0' -> 8, otherwise 10
```

Not a copy of the W parser, not a shared internal worker: **the export's own address**. So `impl.asm`
links change 166 and calls it, rather than re-deriving an eight-group hex grammar with `::`
compression, an embedded IPv4 tail and seventeen Unicode digit blocks.

### The rest of the contract, measured

- **The whole string must be consumed.** The Ex form has no `Terminator` out-parameter, so where the W
  form *accepts* `"::0x1"` and `"::1.2.3.0x5"` — stopping and reporting where — this one refuses them.
  Over fifteen shapes the two differ on exactly those two, and on **zero address bytes**.
- **On failure the address is written and `*ScopeId`/`*Port` are not.** The core writes the address
  before the envelope can know whether the rest of the string is valid. Sentinels confirm it.
- **`:port` only inside brackets** — `"::1:80"` is an address, not an address and a port.
- `']'` without `'['` is an error, and `'['` without `']'` is an error.
- **An empty port is zero**: `"[::1]:"` and `"[::1]:0x"` both give 0.
- port ≤ 65535 and scope ≤ 2³²−1, both refused one past.
- `*Port` is **network** order (80 → `0x5000`); `*ScopeId` is host order.

---

## What this change found in change 166

Composing a landed core into a new caller put it under inputs it had never seen. Two findings, both
recorded in [166's RESULTS.md](../166-rtlipv6stringtoaddressw/RESULTS.md) rather than folded away.

### 1. A 0.79× speed regression on `"::"` — fixed

`"::"` is the shortest valid IPv6 address there is, and 166 had **no row for it**. Put in *this*
table it came out at 7.48 ns against the shipped 5.92. Two causes, both on the `::`-shift path: the
gap zero-fill was a **byte loop** of up to sixteen iterations clearing memory the prologue had already
zeroed, and the entire shift is **dead work** when nothing follows the `::`.

| | before | after |
|---|---|---|
| `::` | 7.48 ns (**0.79×**) | 5.53 ns (**1.08×**) |
| `::1` | 10.57 (1.39×) | 7.62 (**1.98×**) |
| `1::` | 7.97 (2.34×) | 5.81 (**3.24×**) |
| `fe80::1` | 12.62 (3.22×) | 9.82 (**4.16×**) |
| 166's own geomean | 3.348× | **3.835–3.894×** |

### 2. A failure-path divergence, and an overstated claim — recorded, not fixed

166's RESULTS.md claimed its harness compared "STATUS, all 16 address bytes and `*Terminator`". It
does `if(r1==0){ memcmp(…) }` — **the address only when the status is success**. Measured over 55 987
enumerated strings: status differ **0**, `*Terminator` differ **0**, address bytes differ **17 268** —
every one a call the shipped export *failed*, none on a success.

The shipped parser fills the destination as it goes; 166 accumulates in a stack scratch and copies out
once. **The obvious fix was tried and measured worse**: copying `[tmp, tp)` on the error path takes it
to **18 240** and *inverts* it, because a group reaches the destination only when a `:` or `.`
**commits** it. Left in place deliberately — a caller holding `STATUS_INVALID_PARAMETER` has no
defined address to read, and the status and terminator it does act on are identical everywhere. Same
shape of argument as change 243's documented dead region, and section 7 of this change's own harness
reports the number rather than hiding it.

---

## Gate 1 — correctness: PASS

**631 004 cases, 0 mismatches.** Three-way: ours, an independent oracle, and the live
`ntdll!RtlIpv6StringToAddressExW`.

**The oracle models the envelope and calls the live `RtlIpv6StringToAddressW` for the address body** —
the same structure the shipped function has. That makes ours-vs-oracle **isolate the envelope**: a
disagreement there is in the fourteen envelope rules and nowhere else. Ours-vs-live covers both halves.

| | cases |
|---|---|
| pinned shapes: brackets, `%scope`, `:port`, three bases, both bounds, every punctuation error | 70 |
| **every one of the 65 536 UTF-16 units, in eight positions** | 524 280 |
| enumerated over `[]:%01xf.` to length 5 | 66 430 |
| bounds swept: scope 4294967280–4294967310, port 65520–65550 at three bases, plus 24-digit runs | 220 |
| fuzz: address × scope × port × brackets | 40 000 |
| NULL arguments | 4 |

Observables: the NTSTATUS, `*ScopeId` (seeded `0xDEADBEEF`) and `*Port` (seeded `0xBEEF`) on every
case; the sixteen address bytes on every case that succeeded. Section 7 reports the 6 284 failing
cases whose address region differs, for the reason above.

## Gate 2 — speed: PASS

Five consecutive runs: geomean **2.460×, 2.461×, 2.463×, 2.466×, 2.495×**. Worst class 1.14×.

```
size                       ours ns   system ns   ratio   ours GB/s
::                            8.32        9.67    1.16x      0.48
::1                          10.44       18.07    1.73x      0.57
1:2:3:4:5:6:7:8              23.26      115.34    4.96x      1.29
::ffff:1.2.3.4               21.88       87.81    4.01x      1.28
[::1]                        10.48       21.32    2.03x      0.95
[::1]:80                     11.15       24.75    2.22x      1.43
[::1]:65535                  13.07       29.47    2.25x      1.68
[::1]:0x1f90 (hex)           13.00       31.43    2.42x      1.85
[::1]:010 (octal)            11.81       25.07    2.12x      1.52
::1%3                        10.57       23.96    2.27x      0.95
[fe80::1%4294967295]:443     18.36       69.01    3.76x      2.61
full + scope + port          28.93      137.16    4.74x      2.00
FAILS: port 65536            12.64       30.64    2.43x      1.74
FAILS: unclosed [            10.42       18.36    1.76x      0.77
```

The **failure rows are not an afterthought**: a parser is asked to reject far more often than to
accept, and a refusal after the address has already parsed is the whole envelope plus a wasted core
call.

### A benchmark that was measuring itself

This table parked the change twice before it was believed, and both times the harness was at fault.

**First, one saved register too many.** The envelope pushed `rbx`, `rsi`, `rdi`, `r12`, `r13` when only
the cursor has to survive the call — `ScopeId`, `Port` and the bracket flag are written once and read
once and live in the frame's own slots. Eight instructions of prologue and epilogue on every call,
invisible on a long address and decisive on a short one. Fixed; it was not the whole story.

**Then, 4K aliasing.** Every output slot was exactly 4096 bytes, so all 24 outputs a row owns started
at page offset 0 and every store in every row fell in the same L1 set. It showed as rows slower than
strictly harder rows:

| | total | 166's core | apparent "envelope" |
|---|---|---|---|
| `::` | 18.07 ns | 5.64 | **12.4** |
| `[::1]:80` | 12.80 ns | 7.70 | **5.1** |

Same code, less work, more time. Two diagnostics settled it and both are kept in `bench.c`: timing the
three rows **in three different orders** (the numbers followed the row, not the position, so it was not
ordering), and timing 166's core **in this same harness** on exactly the substrings the envelope hands
it (so the envelope's cost is a subtraction, not a guess). Staggering each slot within its page spread
the outputs across 24 sets, and the rows became monotonic in work — `::` 8.32 < `::1` 10.44 <
`[::1]:80` 11.15 — which is the real confirmation. Reporting the 0.87× would have been reporting the
benchmark's own layout. Same family of mistake as change 245's `.bss` placement.

## Gate 3 — Win64 ABI: PASS

**72 changes checked, 0 violations.** `T_250` checks a seam: the cursor lives in `rsi` across a call
into a routine that saves seven registers of its own, and everything else lives in this frame's slots,
so a callee that failed to restore `rsi` — or an unwind descriptor that did not match the 64-byte
allocation — would corrupt a parse that still looked plausible.

## Gate 4 — live substitution: PASS

**263 083 cases each way, 0 mismatches**, in a sacrificial child, validate-first, both prologues
restored byte-identical. `live_subst_ip6.c` is new, and so is the proof: **changes 121, 122 and 166 all
landed with correctness, speed and ABI gates and no hot-patch at all** — the IPv6 parsers had never
been live-substituted.

And it demonstrates the composition on the real binary rather than arguing it from a listing:

```
[166 RtlIpv6StringToAddressW]  ntdll
  under live patch: all match;  our-code calls = 263083
  THE SHIPPED, UNPATCHED RtlIpv6StringToAddressExW was then called 7111
  times and drove OUR core 7111 times -- it reaches the address body by a
  direct internal call to the address this patch overwrote, so one patch
  moved an export that was never touched.
```

Patching `W` **alone** moves the shipped `ExW` onto our core — proved by a counter. Then `ExW` is
patched on its own and the whole envelope is checked: 255 successful parses, 39 carrying a port, 26 a
scope, 65 bracketed.
