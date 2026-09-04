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
| RtlNumberOfSetBits / RtlAreBitsSet | 1.3 / 3.2x | allocator bitmaps |

### ucrtbase (C runtime, loaded in ~220 processes)

| routine | geomean |
|---|---|
| wcslen / strlen | 2.2 / 2.8x |
| memchr | 2.3x |
| wcschr | 2.2x |
| wcscmp / strcmp | 2.9 / 1.3x |

### Parked (honestly recorded — the shipped code is already optimal)

- `memcmp` (ucrtbase): tuned small path; dispatch-floor. `crc32` (ntdll RtlComputeCrc32): already VPCLMULQDQ.

## Proven running live

`live-substitution/` hot-patches the real `ucrtbase`/`ntdll` exports in a running process so calls execute
our assembly, verifies identical results with a counter proving our code ran, then reverts cleanly — for
`wcslen`, `memchr`, `wcschr`, `wcscmp`, `RtlCompareMemory`, `RtlCompareUnicodeString`, and
`RtlUpcaseUnicodeString` (the last a transform that writes an upcased output string via the OS-built
case-fold table, not just a compare).

## Deferred (need dedicated reverse-engineering)

- `RtlCrc64`: a non-standard construction (not a plain reflected CRC; `crc({00},0) != 0`).

The UTF-8 decoder (`RtlUTF8ToUnicodeN`) was in this list; it has since been reverse-engineered and landed
(034) — its exact maximal-subpart malformed rule is documented in that change's RESULTS.md.
