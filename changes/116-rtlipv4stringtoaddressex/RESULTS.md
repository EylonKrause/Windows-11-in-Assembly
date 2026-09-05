# 116 — `ntdll!RtlIpv4StringToAddressExA` — **LANDS** (1.96× geomean)

The `Ex` form of the IPv4 parser (parse-side of [065](../065-rtlipv4addresstostringexa/)): the same
inet_aton address parse as [114](../114-rtlipv4stringtoaddress/) plus an optional `:port`, requiring
the **whole string** to be consumed. There is no `Terminator` output — only STATUS, the address, and
the port (network byte order).

## Contract (matched bit-exact vs live: STATUS + address + network-order Port)
The address is parsed exactly as in 114 and **committed to `*Addr` as soon as it parses** (even if the
port then fails). A trailing `:` introduces the port; nothing else may follow the address. The port
reuses the same number parser (decimal / octal (leading `0`) / hex (`0x`), with the octal-8/9 rule),
but additionally: a **lone octal `0` is rejected** (`:0`→error while `:00`/`:0x0`→port 0), the value
must fit a `USHORT`, and the parse must reach the end of the string. `*Port` is stored `htons`-swapped
and only on full success. Malformed → `STATUS_INVALID_PARAMETER` (0xC000000D).

## De-risking
Because `Ex` has no `Terminator` to match, an independent oracle (reusing 114's validated address
parser + this port tail) matched the live export bit-exactly (STATUS + address + port) over **1M fuzz on
the first pass**; the asm then passed correctness on the first build.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, the 4 address bytes (written iff the address parses), and `*Port`
match the live export and the oracle on 26 edge cases (`:0` vs `:00`, octal/hex ports, `:65536`
overflow, trailing junk, whitespace, address failures) plus **1 500 000 fuzz** strings, `Strict` and
non-`Strict`.

## Benchmark — vs live `ntdll!RtlIpv4StringToAddressExA`
geomean **1.96×** (1.72×–2.19×); ours 16–29 ns vs ntdll 30–58 ns.

| input | strict | ours ns | ntdll ns | ratio |
|---|---|---|---|---|
| `1.2.3.4:80` | 1 | 19.8 | 35.1 | 1.78x |
| `192.168.1.100:65535` | 1 | 28.9 | 58.4 | 2.02x |
| `127.1:8080` | 0 | 16.5 | 33.8 | 2.05x |
| `0x7f.0.0.1:22` | 0 | 20.9 | 45.8 | 2.19x |

## Reproduce
```
changes\116-rtlipv4stringtoaddressex\build.bat
```
