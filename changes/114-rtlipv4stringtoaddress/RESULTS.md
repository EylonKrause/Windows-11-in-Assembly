# 114 — `ntdll!RtlIpv4StringToAddressA` — **LANDS** (1.53× geomean)

The parse-side complement to the landed IPv4/IPv6/MAC **formatters** (059–069): the inet_aton-style
IPv4 text→address parser. ntdll's is a slow scalar routine (~18–44 ns depending on input); this is a
tight scalar parser. Bit-exact including the idiosyncratic malformed-input terminator rules.

## Contract (matched bit-exact vs live: STATUS + 4-byte address + `*Terminator`)
1–4 `.`-separated parts with the classic short forms (`a` = 32-bit, `a.b` = a.(24-bit), `a.b.c` =
a.b.(16-bit), `a.b.c.d`). Each part is decimal, or — when `Strict` is FALSE — octal (leading `0`) or
hex (`0x`). Leading parts must be ≤255; the last part fills the remaining `(5−n)` bytes; the result is
the big-endian assembly. No whitespace or sign is skipped. Malformed input →
`STATUS_INVALID_PARAMETER` (0xC000000D).

The **error-path `*Terminator`** is genuinely idiosyncratic and was reverse-engineered by
reference-first fuzzing against the live export:
- an `8`/`9` **immediately** after a leading `0` errors *at* that digit, but after another octal digit
  it cleanly *terminates* the number (`08`→err, `008`→0);
- an empty component sets `*Terminator = p`, except `p+1` when the offending char is `.` and fewer than
  3 parts have been read;
- a strict `0x` advances the terminator one past the `x`; a strict leading-zero part points at
  `partstart+1`;
- accumulation overflow (`>2³²−1`) points at the offending digit; a range violation points at the
  end of the number; a 5th `.` points at that dot.

## De-risking (reference-first)
An independent C oracle was **validated bit-exact vs the live export** (STATUS + address + terminator)
over 800k fuzz strings before the asm was written — it took 7 refinement passes to pin every terminator
rule. The asm was then ported from that verified spec and **passed correctness on the first build**.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, the 4 address bytes, and `*Terminator` match the live export and
the oracle on 40 hand edge cases (short forms, octal/hex, overflow, the octal-8/9 boundary, whitespace,
strict rejections) plus **1 500 000 fuzz** strings, both `Strict` and non-`Strict`.

## Benchmark — vs live `ntdll!RtlIpv4StringToAddressA`
geomean **1.53×** (1.32×–1.80×); ours 13–27 ns vs ntdll 18–44 ns.

| input | strict | ours ns | ntdll ns | ratio |
|---|---|---|---|---|
| `1.2.3.4` | 1 | 18.9 | 24.9 | 1.32x |
| `192.168.1.100` | 1 | 24.2 | 39.6 | 1.63x |
| `127.1` (short) | 0 | 12.7 | 18.0 | 1.42x |
| `0x7f.0.0.1` | 0 | 19.8 | 35.6 | 1.80x |

## Scope
`RtlIpv4StringToAddressA` (narrow). The wide (`W`) form is a natural next port; the `Ex` forms (which
also parse a `:port`) and the IPv6 parsers are separate, harder targets.

## Reproduce
```
changes\114-rtlipv4stringtoaddress\build.bat
```
