# Summary — Windows 11 in Assembly

A suite of hand-written x86-64 (AVX2 / BMI2 / POPCNT / VPCLMULQDQ) reimplementations of the routines this
Windows 11 install runs hot, each **bit-exact / behavior-identical to the live system function** and
**faster on this machine**, proven change-by-change. See `README.md` for the method and `docs/` for the
platform capture and gates.

## What was optimized, and how much

Every "landed" change beats the shipped `ntdll.dll` / `ucrtbase.dll` function on **every** benchmarked size
class (a regression on any size class parks the change). Geomeans are vs the live system function on the
Ryzen 9 5950X bench.

### Core ntdll (loaded in every process — speeds up all core processes)

| routine | geomean | what it drives |
|---|---|---|
| RtlCompareMemory | 4.4x | registry, cache mgr, memory mgr |
| RtlCompareUnicodeString | 4.0x | registry keys, object names, case-insensitive FS names |
| RtlEqualUnicodeString | 2.0x | filename / key equality |
| RtlPrefixUnicodeString | 2.0x | path/name prefix match |
| RtlHashUnicodeString | 4.5x | name-hash lookups |
| RtlCompareString / RtlEqualString / RtlPrefixString (ANSI) | 4.5 / 4.0 / 2.9x | ANSI name ops |
| RtlUpcaseUnicodeString / RtlDowncaseUnicodeString | 9.1 / 11.2x | name normalization |
| RtlUnicodeStringToAnsiString / RtlAnsiStringToUnicodeString | 12.8 / 11.6x | ANSI interop |
| RtlUnicodeStringToOemString / RtlOemStringToUnicodeString | 7.7 / 5.5x | console / short names |
| RtlUnicodeToMultiByteN / RtlMultiByteToUnicodeN | 3.9 / 4.1x | raw codepage converters |
| RtlUnicodeToOemN / RtlOemToUnicodeN | 6.3 / 4.5x | raw OEM converters |
| RtlUpcaseUnicodeToMultiByteN / RtlUpcaseUnicodeToOemN | 6.5 / 7.4x | upcase+convert |
| RtlUpcaseUnicodeStringToAnsiString | 9.0x | upcase+convert (string) |
| RtlUnicodeToUTF8N / RtlUTF8ToUnicodeN | 2.8 / 3.1x | UTF-16 <-> UTF-8 (full encoder + decoder) |
| RtlCompareMemoryUlong | 5.0x | memory-manager page pattern scan |
| RtlFindCharInUnicodeString | 8.3x (2.5x–13.3x) | path/name set-search (all 8 flags; CI via OS upcase) |
| RtlStringFromGUIDEx | **25.5x** | GUID -> string (COM/registry/ETW); ntdll's is ~420 ns |
| RtlIpv4AddressToStringA | **16.6x** | IPv4 -> dotted decimal (networking); ntdll's is ~100 ns |
| RtlEthernetAddressToStringA | **36.5x** | MAC -> string; ntdll's is ~153 ns (project's largest ratio) |
| RtlIpv4AddressToStringW / RtlEthernetAddressToStringW | **23.7 / 36.0x** | wide IPv4 / MAC formatters |
| RtlIpv6AddressToStringA | **5.9x** | IPv6 -> string (RFC 5952 :: compression + v4-embed; validated 3M) |
| RtlIpv6AddressToStringW | **7.4x** | IPv6 -> string, wide |
| RtlIpv4AddressToStringExA | **9.8x** | IPv4 + port -> string (endpoint logging) |
| RtlIpv4AddressToStringExW | **10.5x** | IPv4 + port -> string, wide |
| RtlConvertSidToUnicodeString | 1.42x | SID -> string (security/ACLs/audit); validated 2M |
| RtlIpv6AddressToStringExA | **6.9x** | IPv6 + scope + port -> string (endpoints) |
| RtlIpv6AddressToStringExW | **8.4x** | IPv6 + scope + port -> string, wide |
| RtlNumberOfSetBits / RtlAreBitsSet | 1.3 / 3.2x | allocator bitmaps |
| RtlIntegerToUnicodeString | 2.9x (2.6x–3.3x) | integer -> decimal/hex formatting (2-digit table) |
| RtlInt64ToUnicodeString | 3.1x (2.9x–3.3x) | 64-bit integer formatting |
| RtlCrc64 | 3.3x (2.0x–4.5x) | reflected CRC-64 (reverse-engineered); slicing-by-8 + 256-bit VPCLMULQDQ fold, ~21 GB/s |
| RtlInitUnicodeString | 1.8x (1.2x–2.2x) | UNICODE_STRING setup (inline AVX2 wcslen, no `call`); called constantly |
| RtlInitString / RtlInitAnsiString | 1.4x (1.1x–3.0x) | ANSI_STRING setup (inline AVX2 strlen) |
| RtlInitUnicodeStringEx | 1.4x (1.1x–2.0x) | validating UNICODE_STRING setup (NTSTATUS) |
| RtlIntegerToChar | 1.4x (1.1x–2.1x) | ANSI integer → string, all bases (2-digit table + direct hex/binary) |
| RtlLargeIntegerToChar | 1.4x (1.1x–1.8x) | 64-bit integer → string, all bases (2-digit table + direct hex/binary) |
| RtlAppendUnicodeToString | 1.4x (1.0x–2.3x) | UNICODE_STRING builder (inline AVX2 wcslen + SIMD copy, no `call`s) |

### ucrtbase (C runtime, loaded in ~220 processes)

| routine | geomean |
|---|---|
| wcslen / strlen | 2.2 / 2.8x |
| memchr | 2.3x |
| wcschr | 2.2x |
| wcscmp / strcmp | 2.9 / 1.3x |
| wcsncmp | 3.3x (~26 GB/s) — bounded UTF-16 compare |
| _ultoa | 1.5x (radix 2–36) — C-runtime integer -> string formatting |
| _ui64toa | 1.6x (radix 2–36) — 64-bit C-runtime integer formatting |
| _itoa / _i64toa | 1.4 / 1.6x (radix 2–36) — signed C-runtime integer formatting |
| _wcsicmp | 7.7x (2.6x–11.5x, ~26 GB/s) — case-insensitive UTF-16 compare (ASCII fold) |
| _stricmp / _wcsnicmp / _strnicmp | 8.7 / 7.4 / 9.3x — case-insensitive compare family (byte + bounded) |
| _memicmp | 10.7x (2.2x–23.7x, ~35 GB/s) — case-insensitive memory compare |
| _strlwr / _strupr / _wcsupr | 8.8 / 8.9 / 6.0x — in-place ASCII case conversion (fold+store) |
| wcspbrk | 4.7x (1.55x–8x; rdtscp ~12x at size) — tokenizer set-membership |
| wcsspn | 5.7x (1.95x–9.4x, 6-char set) — tokenizer span (complement of wcspbrk) |
| wcscspn | 5.6x (1.83x–9.4x, 6-char set) — tokenizer complement span (completes the wide trio) |
| strpbrk / strspn / strcspn | 3.5 / 4.8 / 3.3x — byte (narrow) tokenizer trio, ASCII/UTF-8 parsing |
| _strrev | 7.7x (1.56x–17x) — in-place byte-string reverse (bswap/vpshufb; wins even at 8 B) |
| _wcsrev | 5.0x (1.43x–8.7x) — in-place wide-string reverse (word-reverse vpshufb/vpshuflw) |
| _strset / _strnset | 4.6 / 5.7x — fill string with a char (AVX2 broadcast; _strnset bounded) |
| _wcsset / _wcsnset | 3.7 / 6.4x — fill wide string with a wchar (AVX2 broadcast; _wcsnset bounded) |
| _ultow / _ui64tow | 1.55 / 1.60x (radix 2–36) — unsigned 32/64-bit integer → wide string (2-digit table) |
| _itow(=_ltow) / _i64tow | 1.42 / 1.59x (radix 2–36) — signed 32/64-bit integer → wide string |
| atoi / _atoi64 | 2.1 / 2.2x — string → integer (skip ws, sign, saturating decimal); parse-side of the itoa family, no locale |
| strtol / strtoul | 2.2 / 2.0x — base 0/2–36 parse, endptr + ERANGE bit-exact (256-entry digit table; reference-first) |
| _strtoi64 / _strtoui64 | 1.8 / 1.8x — 64-bit base 0/2–36 parse, endptr + ERANGE (branchless mul-carry overflow guard, no div) |

### crypt32 (certificates / TLS / tokens — loaded by every crypto path)

| routine | geomean |
|---|---|
| CryptBinaryToStringA (base64) | **24.5x** (8.8x–36x) — binary → base64 (SSSE3 Muła); crypt32's is scalar ~0.32 GB/s |
| CryptStringToBinaryA (base64 decode) | **36.3x** (11.7x–56x) — base64 → binary (SSSE3 Muła); crypt32's is scalar ~0.13 GB/s |
| CryptBinaryToStringW (wide base64) | **8.9x** (8.3x–9.2x) — wide binary → base64 (SSSE3 core + reverse in-place widen) |
| CryptStringToBinaryW (wide base64 decode) | **38.7x** (13.7x–59x) — wide base64 → binary (SSSE3 + packuswb narrow) |
| CryptBinaryToStringA (HEXRAW) | **920x** (230x–1655x) — binary → hex (SSSE3 nibble-LUT); crypt32's hex is ~0.01 GB/s (**the project's largest win**) |
| CryptStringToBinaryA (HEXRAW decode) | **259x** (125x–335x) — hex → binary (SSSE3 validate + pmaddubs merge); crypt32's is ~0.017 GB/s |
| CryptBinaryToStringW (wide HEXRAW) | **133x** — wide binary → hex (SSSE3 core + reverse widen) |
| CryptStringToBinaryW (wide HEXRAW decode) | **248x** — wide hex → binary (SSSE3 + packuswb narrow) |
| CryptBinaryToStringA (HEX/HEXASCII/HEXADDR/HEXASCIIADDR) | **150x** (117x–169x) — binary → formatted hexdump (SSSE3 hex core + pshufb space-splice + SIMD ASCII clamp); crypt32's is scalar ~0.011 GB/s |
| CryptBinaryToStringW (wide formatted hex, all 4 modes) | **77x** (35x–465x) — wide formatted hexdump (SSSE3 core + reverse widen) |
| CryptBinaryToStringA (BASE64HEADER/REQUESTHEADER/X509CRLHEADER) | **16.1x** (4.3x–30x) — PEM-header base64 (single-pass inline CRLF) (081 core + BEGIN/END wrapper); crypt32 ~0.3 GB/s |
| CryptBinaryToStringW (wide PEM-header base64, 3 modes) | **57x** (9.9x–348x) — wide PEM-header base64 (SSSE3 core + reverse widen) |
| CryptStringToBinaryA (BASE64HEADER decode) | **11.0x** (3.9x–17.3x) — PEM → binary (082 SSSE3 core + header scan); crypt32 ~0.12 GB/s |
| CryptStringToBinaryW (wide BASE64HEADER decode) | **11.7x** (4.9x–17.0x) — wide PEM → binary (082 SSSE3 + packuswb narrow) |
| CryptStringToBinaryA (BASE64_ANY decode) | **5.3x** (4.0x–5.8x) — auto-detect PEM vs plain base64 (082 SSSE3 core; sets pdwFlags 0/1) |
| CryptStringToBinaryW (wide BASE64_ANY decode) | **5.8x** (5.2x–6.0x) — wide auto-detect (082 SSSE3 + packuswb) |

### Parked (honestly recorded — the shipped code is already optimal)

- `memcmp` (ucrtbase): tuned small path; dispatch-floor. `crc32` (ntdll RtlComputeCrc32): already VPCLMULQDQ.
- `_wcslwr` (ucrtbase): correct + 2-6x >= 32 B, but ucrtbase's tight 8-wchar small path wins at size 8 (dispatch floor, narrowed to 0.91x); the identical-structure `_wcsupr` lands.
- `strstr` / `wcsstr` (ucrtbase): **already SSE4.2 `pcmpistri`** (hardware substring search, ~22 GB/s). Our AVX2 two-char anchor streams at ~28 GB/s and beats it at >=4 KB (up to 1.27x) but loses below ~2 KB to `pcmpistri`'s low per-call cost, so a size class regresses. See 089.
- `strncmp` (ucrtbase): **already a tuned aligned SWAR** (aligns, then 8-byte "has-zero" reads with no per-read page check). Our AVX2 wins large (128 B–32 KB, 1.46×–2.06×) and ties at 8 B, but loses the 32-byte class (0.91×) to ucrtbase's four aligned SWAR reads. See 099.

## Proven running live

`live-substitution/` hot-patches the real `ucrtbase`/`ntdll` exports in a running process so calls execute
our assembly, verifies identical results with a counter proving our code ran, then reverts cleanly — for
`wcslen`, `memchr`, `wcschr`, `wcscmp`, `RtlCompareMemory`, `RtlCompareUnicodeString`, and
`RtlUpcaseUnicodeString` (a transform that writes an upcased output string via the OS-built case-fold
table), plus `_wcsicmp`/`_stricmp`/`_memicmp` (case-insensitive compares) and `wcspbrk`/`strpbrk`
(tokenizer set-search) and the integer formatters `RtlIntegerToUnicodeString`/`RtlInt64ToUnicodeString` — 14 functions in all, each verified identical under a live hot-patch.

## Deferred (need dedicated reverse-engineering)

- **`RtlIpv6StringToAddressW`** and **`RtlIpv6StringToAddressExW`** (the wide IPv6 parse-side forms). The
  narrow **`RtlIpv6StringToAddressA` (121, 4.91×, up to 10.8×)** and **`RtlIpv6StringToAddressExA` (122,
  4.17×, up to 7.5×)** have both since been reverse-engineered and landed — 121 is the full
  `::`/embedded-IPv4 grammar + all the Windows-lenient stop/terminator rules (5M-fuzz bit-exact); 122
  wraps that core with `[addr%zone]:port` bracket/scope/port handling (2M-fuzz bit-exact). Both wide
  forms are **scoped out**, not deferred: unlike IPv4-W/MAC-W they recognize Unicode decimal digits
  (Arabic-Indic, fullwidth, … by value) as digits — the same CRT/OS Unicode-digit-table blocker that
  scopes out `_wtoi`.

  The parse-side family is now **complete**: `RtlIpv4StringToAddressA/W` (114/115) + `…ExA/W` (116/117),
  `RtlGUIDFromString` (118), `RtlEthernetStringToAddressA/W` (119/120), `RtlIpv6StringToAddressA` (121)
  and `RtlIpv6StringToAddressExA` (122). Only the two IPv6 wide forms remain, and only for the
  Unicode-digit reason, not RE difficulty.

The UTF-8 decoder (`RtlUTF8ToUnicodeN`) was in this list; it has since been reverse-engineered and landed
(034) — its exact maximal-subpart malformed rule is documented in that change's RESULTS.md. `RtlCrc64` was
also here; it has now been reverse-engineered **and landed** (076: reflected CRC-64, poly
`0x9A6C9329AC4BC9B5`, `~init`/`~out`, validated 400k vs the live export; hybrid slicing-by-8 + 256-bit
VPCLMULQDQ fold, **3.29× geomean**, ~21 GB/s vs ntdll's 4.8 at size).
