# 121 — `ntdll!RtlIpv6StringToAddressA` — **LANDS** (4.91× geomean, up to 10.8×)

The parse-side complement to the landed IPv6 **formatter** [063 RtlIpv6AddressToStringA](../063-rtlipv6addresstostringa/),
and the last and hardest of the IP/MAC/GUID parser family. Parse an IPv6 address — 8 groups of 1–4 hex,
`:`-separated, one `::` zero-compression, optional trailing embedded IPv4 — into 16 network-order bytes.
ntdll's is a very slow scalar routine (~90–283 ns); this is a scalar parser with a 16-byte stack
scratch (~14–26 ns).

## Contract (matched bit-exact vs live: STATUS + 16 bytes + `*Terminator`)
A BSD-`inet_pton6` core adapted to Windows's lenient stop rules and idiosyncratic terminator, all
reverse-engineered by reference-first fuzzing against the live export (11 refinement passes). Highlights:
- one `::` sets the zero-fill point; the address body is 8 groups, shifted to the end at close;
- Windows **stops (success)** rather than erroring at a 2nd `::`, a 9th group, or a full address — the
  `*Terminator` lands at the group-ending `:` (or after a `::` that fills the last group);
- a `:`/`.` where a hex digit is expected is a *dangling error* unless the address is already full
  (then a clean stop); a leading single `:` sets `*Terminator = S`;
- **embedded IPv4**: a `.` in an all-decimal group with room (`tp+4 ≤ endp − (::?2:0)`) parses 4 decimal
  octets (leading zeros ok, ≤ 3 digits, ≤ 255) into the last 4 bytes;
- several malformed paths (a `>4`-hex or `>255`/`>3`-digit octet in positions 1–3) return the error
  **without** writing `*Terminator` — matched exactly.

## De-risking (reference-first)
An independent C oracle was validated **bit-exact vs the live export** (STATUS + 16 bytes + terminator)
over **5 million fuzz** strings — core, `::`, and embedded-IPv4 tails — before the asm was written; the
asm (the largest in the repo) then passed correctness on the **first build**.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, the 16 address bytes, and `*Terminator` match the live export and
the oracle on 37 edge cases (compression, embedded IPv4, over-long groups/octets, the octal/overflow
terminator quirks, leading/2nd `::`) plus **5 000 000 fuzz** strings.

## Benchmark — vs live `ntdll!RtlIpv6StringToAddressA`
geomean **4.91×** (1.76×–**10.77×**); ours 14–26 ns vs ntdll 24–283 ns.

| input | ours ns | ntdll ns | ratio |
|---|---|---|---|
| `2001:0db8:…:5678` (full) | 26.27 | 282.92 | 10.77x |
| `2001:db8::1` | 15.79 | 89.34 | 5.66x |
| `::1` | 13.79 | 24.22 | 1.76x |
| `::ffff:1.2.3.4` | 24.46 | 132.89 | 5.43x |

## Scope
`RtlIpv6StringToAddressA` (narrow). The wide `W` form is **scoped out**: unlike the IPv4/MAC wide
parsers, `RtlIpv6StringToAddressW` recognizes **Unicode decimal digits** (Arabic-Indic U+0660–9,
fullwidth U+FF10–9, … by digit value) as hex digits, so a bit-exact reimpl would need the CRT/OS Unicode
digit table — the same blocker that scopes out `_wtoi` (see [109](../109-atoi64/)). The `Ex` forms
(`[addr]:port` / `%zone`) remain a separate target.

## Reproduce
```
changes\121-rtlipv6stringtoaddress\build.bat
```
