# 166 — `ntdll!RtlIpv6StringToAddressW` — **LANDS** (3.84–3.89× geomean, up to 6.59×)

> **Revised while landing [change 250](../250-rtlipv6stringtoaddressexw/)**, which composes this
> core into `RtlIpv6StringToAddressExW` and put it under inputs this change had never measured.
> Three things came out of that, and all three are recorded below rather than folded away:
> a **0.79× speed regression on `"::"`** (now fixed, and the geomean rose 3.62× → 3.84–3.89×),
> an **overstated correctness claim** in this file (now corrected), and a **failure-path
> divergence** from the shipped export that is real, quantified, and deliberately left in place.

The wide twin of [121 `RtlIpv6StringToAddressA`](../121-rtlipv6stringtoaddress/), which 121's own
RESULTS.md had **scoped out** as untouchable:

> *unlike the IPv4/MAC wide parsers, `RtlIpv6StringToAddressW` recognizes Unicode decimal digits
> (Arabic-Indic U+0660–9, fullwidth U+FF10–9, … by digit value) as hex digits, so a bit-exact reimpl
> would need the CRT/OS Unicode digit table*

That was the right instinct and the wrong conclusion. The set is not an opaque table: swept over **all
65536 UTF-16 units**, it is exactly **17 contiguous blocks of ten**, plus the three ASCII runs. Twenty
runs, 192 units, no exceptions:

```
0030..0039  0041..0046  0061..0066          <- ASCII
0660  06F0  0966  09E6  0A66  0AE6  0B66  0C66  0CE6
0D66  0E50  0ED0  0F20  1040  17E0  1810  FF10      <- +0..9 each
```

That is the Unicode 3.0-era `Nd` set, frozen in ntdll for decades — no Balinese (U+1B50), no Vai
(U+A620), none of the later additions. Seventeen `dw` and a five-instruction loop reproduce it, and the
correctness harness re-derives the whole classification from the live export on every build.

## The rule the wide export actually follows
Two halves, and keeping them apart is the whole trick.

**The structure scan is ASCII-only.** It compares the *full* 16-bit unit — there is no low-byte
aliasing, so U+0141 is not `'A'` and U+013A is not `':'` — and the 256-entry hex table is indexed only
behind an explicit `< 256` gate. That fixes the separators, the group count `seen`, the octet digit
count `nd`, and `*Terminator`, all exactly as in the ANSI routine.

**The value is a re-parse of the token, and it does not stop where the scan stopped.** A number helper
re-reads from the token start, honours a `0x`/`0X` prefix, accepts ASCII hex **plus those 17 Unicode
blocks**, accumulates in 32 bits and saturates to `0xFFFF` the moment a shift would overflow signed
32-bit. It moves a private cursor: `seen`, `nd` and `*Terminator` never see it.

$$
\texttt{"::1<U+0660>2"} \;\longrightarrow\; \text{value } \texttt{0x102}, \quad
\texttt{*Terminator} = S+3 \ \text{(still at the U+0660)}
$$

```
"::1<U+0660>"          -> 0x0010     *Terminator at the U+0660 (offset 3)
"::1234<U+0665>"       -> 0x2345     (0x12345, low 16 bits)
"::1<U+FF41>"          -> 0x0001     fullwidth 'a' is not a digit; it stops the helper dead
"::1.2.3.4<U+0665>"    -> last octet 45, base 10, *Terminator still at the U+0665
"::0x<U+0661>2"        -> 0x0012     the prefix is honoured on the wide side too
```

Only the **last** group can ever hold one of these: the ASCII scan stops at the first non-ASCII unit,
so no separator after it is ever reached. The embedded-IPv4 octet helper is the same idea in base 10,
with ntdll's 65535 cap and **no** prefix — hence `"::1.2.3.0x5"` is `0`.

## What this cost change 121
The wide export reaches shared code with inputs an ANSI generator does not produce, so building this
change **found four defects in the already-landed 121** — a `>4`-digit final group not writing
`*Terminator`, the IPv4 separator being checked after the octet instead of before, the `0x` prefix
being ignored entirely, and a `>4`-digit group followed by a second `::` not writing `*Terminator`.
All four were confirmed against the live **ANSI** export, fixed in 121, and 121's fuzz was extended
with the two generators that reach them. See [121's RESULTS.md](../121-rtlipv6stringtoaddress/RESULTS.md).

## Method
`impl.asm` is 121's parser transliterated to UTF-16 — every `movzx …, byte ptr` becomes `word ptr`,
every `inc rsi` becomes `add rsi, 2`, every character compare uses the full 32-bit zero-extended unit —
plus the number helper and a `ud_val` subroutine for the 17 blocks. `ud_val` is reached **only** on
non-ASCII input, so an all-ASCII address never pays for any of it; the hot path is 121's, one 16-bit
load per unit.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**, comparing STATUS and `*Terminator` (including *whether* it was written,
which several error paths deliberately skip) on **every** case, and all 16 address bytes on every case
that **succeeded**, against the live `W` export **and** an independent C oracle. Coverage:

- **76 edge cases** — 121's full ANSI corpus lifted to UTF-16, plus the four defect shapes above;
- **36 trap units × every position of every base string, substituted *and* inserted** — units whose low
  byte aliases a meaningful ASCII character (U+012E→`'.'`, U+013A→`':'`, U+0130→`'0'`, U+0141→`'A'`),
  one member of each of the 17 digit blocks, a lone surrogate, and the ends of the range;
- **all 65 535 non-zero units × 24 templates** — every template chosen to drive a different branch that
  can see a non-ASCII unit: mid-group, group start, after `0x`, inside each IPv4 octet, after a full
  address, at a dangling `:`, and past a `>4`-digit group;
- an **exhaustive sweep of every string of length 0–5** over an alphabet containing a non-ASCII unit;
- **5 000 000 fuzz** strings from 121's two generators, with a trap unit injected into one in eight;
- a **NOACCESS page guard** placed one unit past the terminating NUL, for every base string and every
  trap substitution, so a single unit of over-read dies immediately.

### The address bytes are compared on SUCCESS only — and why that sentence used to be missing

This file previously said the harness compared "STATUS, all 16 address bytes and `*Terminator`". The
harness does `if(r1==0){ memcmp(a1,a2,16) … }` — the address is compared **only when the status is
success**. The claim overstated it, and change 250 found the gap by composing this core into the Ex
form, which hands the caller's buffer straight down and can then fail in its *own* envelope.

Measured directly, over 55 987 enumerated strings on the alphabet `: . 0 1 a f` to length 6:

| | |
|---|---|
| status differ | **0** |
| `*Terminator` differ | **0** |
| address bytes differ | **17 268** — every one a call the shipped export **failed**, none on a success |

The shipped parser fills the destination **as it goes**, so a call that fails part-way keeps whatever
it had committed: `"f:"` leaves `00 0F`, `"0:"` leaves `00 00`, `"1."` leaves `01`, `"::1."` leaves
`00 00 01`. This implementation accumulates into a stack scratch and copies out once.

**The obvious fix is wrong, and was tried and measured rather than assumed.** Copying `[tmp, tp)` on
the error path takes the divergence from 17 268 to **18 240** and *inverts* it — we then write for
bare groups like `"0"`, `"10"`, `"a0"` where the shipped one writes nothing at all. A group reaches
the destination only when a `:` or `.` **commits** it, not when the scan has merely accumulated it,
and reproducing that write schedule means deriving it from the outside as its own enumerated study.

It is left undone deliberately: a caller receiving `STATUS_INVALID_PARAMETER` has no defined address
to read, the two things such a caller acts on — the status and the terminator — are identical in all
55 987 cases, and every call that succeeds is byte-identical. Same shape of argument as change 243's
documented dead region.

### A 0.79× regression on `"::"`, found by change 250 and fixed

`"::"` is the shortest valid IPv6 address there is, and this change had **no row for it** — the four
benchmark rows are a full address, a compressed one, the loopback and a v4-mapped one, all of which
have real parsing work to amortise against. Change 250 put `"::"` in *its* table and the row came out
**0.79×**: 7.48 ns against the shipped 5.92.

Two causes, both on the `"::"`-shift path:

1. **The gap zero-fill was a byte loop.** Up to sixteen iterations of four instructions, clearing
   memory the prologue had already zeroed. For `"::"` the gap is the whole sixteen bytes and there is
   nothing else to do, so the loop *was* the function. Replaced with overlapping stores per width.
2. **The shift is entirely dead when nothing follows the `::`.** When `tp == colonp` there are no
   groups to move and the gap has never been written, so the copy-out, the zero-fill and the copy-back
   are all no-ops on every address ending in `::`.

| | before | after |
|---|---|---|
| `::` | 7.48 ns (**0.79×**) | 5.53 ns (**1.08×**) |
| `::1` | 10.57 (1.39×) | 7.62 (**1.98×**) |
| `1::` | 7.97 (2.34×) | 5.81 (**3.24×**) |
| `fe80::1` | 12.62 (3.22×) | 9.82 (**4.16×**) |
| geomean, this change's own four rows | 3.348× | **3.835–3.894×** |

Correctness is unchanged by both edits — the full harness still passes, and the failure-path
divergence stays at exactly 17 268, which is what proves the fixes are behaviour-neutral.

## Benchmark — vs live `ntdll!RtlIpv6StringToAddressW`
geomean **3.62×**, every input better:

| input | ours ns | ntdll ns | ratio |
|---|---|---|---|
| `2001:0db8:…:5678` (full) | 37.12 | 253.95 | **6.84x** |
| `2001:db8::1` | 19.57 | 80.11 | 4.09x |
| `::1` | 15.13 | 22.44 | 1.48x |
| `::ffff:1.2.3.4` | 27.13 | 111.55 | 4.11x |

Lower than 121's 4.66× for the honest reason: the wide export is itself faster than the ANSI one
(254 ns vs 283 ns on the full address), while ours reads twice the bytes.

## Reproduce
```
changes\166-rtlipv6stringtoaddressw\build.bat
```
