# 122 — `ntdll!RtlIpv6StringToAddressExA` — **LANDS** (4.17× geomean, up to 7.5×)

The "Ex" IPv6 parser: the address body of [121 `RtlIpv6StringToAddressA`](../121-rtlipv6stringtoaddress/)
wrapped with optional `[` … `]` brackets, an optional `%<scope>` zone id, and (inside brackets) an
optional `:<port>` — i.e. it parses a full `[addr%zone]:port` URL authority. ntdll's is the same slow
scalar family (~37–286 ns); this reuses 121's validated 16-byte-stack-scratch core plus a small
bracket/scope/port wrapper (~19–38 ns).

## Contract (matched bit-exact vs live: STATUS + 16 bytes + `ScopeId` + `Port`)
The address body is the exact 121 core (unchanged rules). On top of it, reverse-engineered
reference-first against the live export:
- a leading `[` turns on bracket mode; only bracket mode allows a trailing `:port`;
- after the address, an optional `%<decimal>` scope — **decimal digits only**, value ≤ `2^32−1`
  (else `STATUS_INVALID_PARAMETER`); stored in `*ScopeId` host-order;
- in bracket mode a `]` is then required; an optional `:port` follows;
- **port** is octal/hex/decimal like the IPv4-Ex port: leading `0x`→hex, leading `0`→octal, else
  decimal; value ≤ 65535; a lone `0x` with no hex digit, or an empty `:` port, yields port 0; stored
  **network-order** in `*Port`;
- the **whole string** must be consumed — any trailing byte is an error.

## De-risking (reference-first)
The oracle (`ref_ip6exa` = 121's `ref_ip6` core + scope + `pport`) was validated **bit-exact vs the
live export** (STATUS + 16 bytes + ScopeId + Port) over **1.5 million fuzz** `[addr%scope]:port`
strings before the asm. Only one class missed on the first reference pass — a `0x`-with-no-hex port —
fixed to yield port 0. The asm then passed after a single fix (a register-clobber; see below).

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, 16 address bytes, `ScopeId`, and network-order `Port` match the
live export and the oracle on **43 edge cases** (brackets, `%scope` decimal/overflow, `:port`
octal/hex/decimal, `0x`/empty-port, embedded-IPv4 + port, unbracketed-with-`:`) plus **2 000 000 fuzz**
strings.

## Benchmark — vs live `ntdll!RtlIpv6StringToAddressExA`
geomean **4.17×** (1.94×–**7.48×**); ours 19–38 ns vs ntdll 37–286 ns.

| input | ours ns | ntdll ns | ratio |
|---|---|---|---|
| `[2001:db8:…:5678]:443` (full+port) | 38.22 | 286.00 | 7.48x |
| `[2001:db8::1%12]:8080` (compressed+scope+port) | 24.67 | 115.78 | 4.69x |
| `[::1]:80` (loopback+port) | 19.13 | 37.11 | 1.94x |
| `[::ffff:1.2.3.4]:53` (v4mapped+port) | 32.67 | 145.54 | 4.45x |

## Notes
- The asm reuses 121's core labels/logic verbatim (assembling into a 16-byte stack scratch), so `r9`
  holds the `hexval` table across the whole address parse. The `::` shift-copy at close uses `r9` as a
  byte-move scratch, so the table pointer is **reloaded** (`lea r9, hexval`) before the port parser —
  the one fix between first build (segfault on `[::1]:80`) and PASS.

## Scope
`RtlIpv6StringToAddressExA` (narrow). The wide `Ex**W**` form is **scoped out** for the same reason as
121's `W`: `RtlIpv6StringToAddressExW` recognizes **Unicode decimal digits** in the address/scope, which
an ASCII-only reimpl can't match bit-exact without the OS Unicode-digit table (the `_wtoi`/`wcstol`
blocker — see [109](../109-atoi64/), [121](../121-rtlipv6stringtoaddress/)). This closes the IP/MAC/GUID
parse-side family.

## Reproduce
```
changes\122-rtlipv6stringtoaddressex\build.bat
```
