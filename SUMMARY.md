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

### crypt32 (certificates / TLS / tokens — loaded by every crypto path)

| routine | geomean |
|---|---|
| CryptBinaryToStringA (base64) | **24.5x** (8.8x–36x) — binary → base64 (SSSE3 Muła); crypt32's is scalar ~0.32 GB/s |

### Parked (honestly recorded — the shipped code is already optimal)

- `memcmp` (ucrtbase): tuned small path; dispatch-floor. `crc32` (ntdll RtlComputeCrc32): already VPCLMULQDQ.
- `_wcslwr` (ucrtbase): correct + 2-6x >= 32 B, but ucrtbase's tight 8-wchar small path wins at size 8 (dispatch floor, narrowed to 0.91x); the identical-structure `_wcsupr` lands.

## Proven running live

`live-substitution/` hot-patches the real `ucrtbase`/`ntdll` exports in a running process so calls execute
our assembly, verifies identical results with a counter proving our code ran, then reverts cleanly — for
`wcslen`, `memchr`, `wcschr`, `wcscmp`, `RtlCompareMemory`, `RtlCompareUnicodeString`, and
`RtlUpcaseUnicodeString` (a transform that writes an upcased output string via the OS-built case-fold
table), plus `_wcsicmp`/`_stricmp`/`_memicmp` (case-insensitive compares) and `wcspbrk`/`strpbrk`
(tokenizer set-search) and the integer formatters `RtlIntegerToUnicodeString`/`RtlInt64ToUnicodeString` — 14 functions in all, each verified identical under a live hot-patch.

## Deferred (need dedicated reverse-engineering)

- *(none outstanding.)*

The UTF-8 decoder (`RtlUTF8ToUnicodeN`) was in this list; it has since been reverse-engineered and landed
(034) — its exact maximal-subpart malformed rule is documented in that change's RESULTS.md. `RtlCrc64` was
also here; it has now been reverse-engineered **and landed** (076: reflected CRC-64, poly
`0x9A6C9329AC4BC9B5`, `~init`/`~out`, validated 400k vs the live export; hybrid slicing-by-8 + 256-bit
VPCLMULQDQ fold, **3.29× geomean**, ~21 GB/s vs ntdll's 4.8 at size).
