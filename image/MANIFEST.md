# Image manifest — hand-ASM reimplementations mapped to the Win11 System32 tree

120 `.asm` files across 4 System32 DLL folders (materialized under `tree/Windows/System32/`).

## Windows/System32/ntdll.dll — 42 functions
`RtlAnsiStringToUnicodeString`, `RtlAreBitsSet`, `RtlCompareMemory`, `RtlCompareMemoryUlong`, `RtlCompareString`, `RtlCompareUnicodeString`, `RtlConvertSidToUnicodeString`, `RtlCrc64`, `RtlDowncaseUnicodeString`, `RtlEqualString`, `RtlEqualUnicodeString`, `RtlEthernetAddressToStringA`, `RtlEthernetAddressToStringW`, `RtlFindCharInUnicodeString`, `RtlHashUnicodeString`, `RtlInt64ToUnicodeString`, `RtlIntegerToUnicodeString`, `RtlIpv4AddressToStringA`, `RtlIpv4AddressToStringExA`, `RtlIpv4AddressToStringExW`, `RtlIpv4AddressToStringW`, `RtlIpv6AddressToStringA`, `RtlIpv6AddressToStringExA`, `RtlIpv6AddressToStringExW`, `RtlIpv6AddressToStringW`, `RtlMultiByteToUnicodeN`, `RtlNumberOfSetBits`, `RtlOemStringToUnicodeString`, `RtlOemToUnicodeN`, `RtlPrefixString`, `RtlPrefixUnicodeString`, `RtlStringFromGUIDEx`, `RtlUTF8ToUnicodeN`, `RtlUnicodeStringToAnsiString`, `RtlUnicodeStringToOemString`, `RtlUnicodeToMultiByteN`, `RtlUnicodeToOemN`, `RtlUnicodeToUTF8N`, `RtlUpcaseUnicodeString`, `RtlUpcaseUnicodeStringToAnsiString`, `RtlUpcaseUnicodeToMultiByteN`, `RtlUpcaseUnicodeToOemN`

## Windows/System32/ucrtbase.dll — 35 functions
`_i64toa`, `_i64tow`, `_itoa`, `_itow`, `_memicmp`, `_stricmp`, `_strlwr`, `_strnicmp`, `_strnset`, `_strrev`, `_strset`, `_strupr`, `_ui64toa`, `_ui64tow`, `_ultoa`, `_ultow`, `_wcsicmp`, `_wcsnicmp`, `_wcsnset`, `_wcsrev`, `_wcsset`, `_wcsupr`, `memchr`, `strcmp`, `strcspn`, `strlen`, `strpbrk`, `strspn`, `wcschr`, `wcscmp`, `wcscspn`, `wcslen`, `wcsncmp`, `wcspbrk`, `wcsspn`

## Windows/System32/msvcrt.dll — 35 functions
`_i64toa`, `_i64tow`, `_itoa`, `_itow`, `_memicmp`, `_stricmp`, `_strlwr`, `_strnicmp`, `_strnset`, `_strrev`, `_strset`, `_strupr`, `_ui64toa`, `_ui64tow`, `_ultoa`, `_ultow`, `_wcsicmp`, `_wcsnicmp`, `_wcsnset`, `_wcsrev`, `_wcsset`, `_wcsupr`, `memchr`, `strcmp`, `strcspn`, `strlen`, `strpbrk`, `strspn`, `wcschr`, `wcscmp`, `wcscspn`, `wcslen`, `wcsncmp`, `wcspbrk`, `wcsspn`

## Windows/System32/crypt32.dll — 8 functions
`CryptBinaryToStringA`, `CryptBinaryToStringA`, `CryptBinaryToStringW`, `CryptBinaryToStringW`, `CryptStringToBinaryA`, `CryptStringToBinaryA`, `CryptStringToBinaryW`, `CryptStringToBinaryW`

