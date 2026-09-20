# 121 — `ntdll!RtlIpv6StringToAddressA` — **LANDS** (4.66× geomean, up to 10.0×)

The parse-side complement to the landed IPv6 **formatter** [063 RtlIpv6AddressToStringA](../063-rtlipv6addresstostringa/),
and the last and hardest of the IP/MAC/GUID parser family. Parse an IPv6 address — 8 groups of 1–4 hex,
`:`-separated, one `::` zero-compression, optional trailing embedded IPv4 — into 16 network-order bytes.
ntdll's is a very slow scalar routine (~24–283 ns); this is a scalar parser with a 16-byte stack
scratch (~14–28 ns).

## Corrected while building [166 `RtlIpv6StringToAddressW`](../166-rtlipv6stringtoaddressw/)
This change originally shipped with **four wrong behaviours**, all of them on paths its 5M fuzz could
not generate. Building the wide twin exposed them, because the wide export reaches the same code with
inputs the ANSI generator never produced. Every one was then confirmed against the live **ANSI** export
and fixed here; the fuzz now generates all four shapes.

| # | Shape the generator never emitted | What the live export does | What 121 did |
|---|---|---|---|
| 1 | a **final** group of more than 4 hex digits (`"12345"`, `"abcdef"`) | `*Terminator = p`, then error | left `*Terminator` **unchanged** |
| 2 | an invalid octet whose **separator is also wrong** (`"::1.2222x.3.4"`) | checks the `'.'` **first**: `*Terminator = q`, error | validated the octet first → left it unchanged |
| 3 | a token with a **`0x`/`0X` prefix** (`"::0x9"`) | value **9** — the value is a *re-parse of the token*, not what the scan accumulated | stored the scan's value, **0** |
| 4 | a **>4-hex group followed by a second `::`** (`"::8D62b6C::"`) | `*Terminator = p`, then error | left `*Terminator` unchanged |

Item 3 is the structural one, and it is what the wide routine turns into a much bigger deal. **The
value the routine stores is not the number its scan built.** The scan walks ASCII hex and stops dead at
the first character it does not recognise — that is what fixes `seen` (the >4-digit test) and
`*Terminator`. The *value* then comes from a separate number helper that re-reads the token **from its
start**, honours a `0x`/`0X` prefix, and keeps going past where the scan gave up, accumulating in 32
bits and saturating to `0xFFFF` the moment a shift would overflow signed 32-bit:

```
"::0x9"          -> value 9,       *Terminator at the 'x'   (offset 3)
"::0x1234567"    -> value 0x4567   (low 16 bits of 0x1234567)
"::0x89abcdef"   -> value 0xFFFF   (0x089ABCDE << 4 overflows)
"::0xx9"         -> value 0        (prefix eaten, then nothing)
"::00x9"         -> value 0        (no prefix: the second char is not 'x')
```

The `nd > 3` octet test likewise counts only what the *scan* saw, while the octet's value comes from the
same kind of re-read in base 10 with ntdll's 65535 cap and **no** prefix — so `"::1.2.3.0x5"` is `0`.

## Contract (matched bit-exact vs live: STATUS + 16 bytes + `*Terminator`)
A BSD-`inet_pton6` core adapted to Windows's lenient stop rules and idiosyncratic terminator, all
reverse-engineered by reference-first fuzzing against the live export. Highlights:
- one `::` sets the zero-fill point; the address body is 8 groups, shifted to the end at close;
- Windows **stops (success)** rather than erroring at a 2nd `::`, a 9th group, or a full address — the
  `*Terminator` lands at the group-ending `:` (or after a `::` that fills the last group);
- a `:`/`.` where a hex digit is expected is a *dangling error* unless the address is already full
  (then a clean stop); a leading single `:` sets `*Terminator = S`;
- **embedded IPv4**: a `.` in an all-decimal group with room (`tp+4 ≤ endp − (::?2:0)`) parses 4 decimal
  octets (leading zeros ok, ≤ 3 scanned digits, ≤ 255) into the last 4 bytes; for octets 1–3 the `'.'`
  separator is checked **before** the octet is validated;
- a `>4`-hex group before a `:` and a `>255`/`>3`-digit octet in positions 1–3 return the error
  **without** writing `*Terminator` — except that a `>4`-hex group followed by a *second* `::` writes it.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, the 16 address bytes, and `*Terminator` match the live export and
the independent C oracle on **92 edge cases** (compression, embedded IPv4, over-long groups and octets,
`0x`-prefixed tokens, the saturation boundary, the octal/overflow terminator quirks, leading and second
`::`) plus **6 000 000 fuzz** strings from four generators — the original core and embedded-IPv4 tails,
plus two added here that emit **long final groups with `x`/`X` in the alphabet** and **IPv4 tails whose
octets are 1–5 digits with `.`/`:`/`x`/`-` separators**. Those two generators are what a fuzz for this
routine actually needs: the first three defects above are invisible without them.

## Benchmark — vs live `ntdll!RtlIpv6StringToAddressA`
geomean **4.66×** (1.72×–**10.03×**); ours 14–28 ns vs ntdll 24–283 ns.

| input | ours ns | ntdll ns | ratio |
|---|---|---|---|
| `2001:0db8:…:5678` (full) | 28.23 | 283.11 | 10.03x |
| `2001:db8::1` | 17.35 | 89.55 | 5.16x |
| `::1` | 14.23 | 24.44 | 1.72x |
| `::ffff:1.2.3.4` | 25.14 | 133.11 | 5.30x |

## Scope
`RtlIpv6StringToAddressA` (narrow). The wide `W` form was previously scoped out here on the grounds
that it recognises Unicode decimal digits — that turned out to be tractable and is now landed as
[166](../166-rtlipv6stringtoaddressw/): the set is exactly **17 contiguous blocks of ten**, enumerated
over all 65536 units. The `Ex` forms (`[addr]:port` / `%zone`) remain a separate target.

## Reproduce
```
changes\121-rtlipv6stringtoaddress\build.bat
```

## Unresolved finding — the caller's buffer after a FAILED parse (2026-09-20)

**Not fixed. Measured, named, and printed on every live run so it cannot be forgotten.**

On a parse that fails, the shipped export **writes partial data** into the caller's address buffer
and this implementation leaves it untouched. `RtlIpv6StringToAddressA("182.77.169.58", ...)` returns
`STATUS_INVALID_PARAMETER` with the same terminator either way, and ntdll has left `B6 4D A9` —
182, 77, 169 — in the first three bytes.
[`probes/failbuf.c`](probes/failbuf.c) shows it directly.

[`live-substitution/live_subst_parseaddr.c`](../../live-substitution/live_subst_parseaddr.c) counts
it separately from its verdict: **13822 of 40000 calls**, with the NTSTATUS and the terminator
agreeing on every one of them. The successful parses are byte-exact and do carry the verdict.

**Why it is left alone, for now.** This is the *safe* direction — we do not write to memory the
caller was told we failed on, where the Ethernet pair (119/120) had the dangerous direction and was
fixed the same day. Matching it means reproducing ntdll's *abandonment* behaviour exactly rather
than its success behaviour: how far it got before giving up, on every malformed shape in a grammar
with compression, embedded IPv4, scope ids and ports. That is a piece of reverse engineering in its
own right and is not attempted here.

Anyone narrowing this should start from `probes/failbuf.c` and the counter in the live harness,
which will drop to zero when it is right.

