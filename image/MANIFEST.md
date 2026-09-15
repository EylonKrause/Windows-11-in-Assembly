# Image manifest — hand-ASM reimplementations mapped to the Win11 System32 tree

226 `.asm` files across 8 System32 DLL folders (materialized under `tree/Windows/System32/`).

## Windows/System32/ucrtbase.dll — 76 functions
`_atoi64`, `_i64toa`, `_i64toa_s`, `_i64tow`, `_i64tow_s`, `_itoa`, `_itoa_s`, `_itow`, `_itow_s`, `_memccpy`, `_memicmp`, `_stricmp`, `_strlwr`, `_strlwr_s`, `_strnicmp`, `_strnset`, `_strnset_s`, `_strrev`, `_strset`, `_strset_s`, `_strtoi64`, `_strtoui64`, `_strupr`, `_strupr_s`, `_swab`, `_ui64toa`, `_ui64toa_s`, `_ui64tow`, `_ui64tow_s`, `_ultoa`, `_ultoa_s`, `_ultow`, `_ultow_s`, `_wcsicmp`, `_wcslwr_s`, `_wcsnicmp`, `_wcsnset`, `_wcsnset_s`, `_wcsrev`, `_wcsset`, `_wcsset_s`, `_wcstoi64`, `_wcstoui64`, `_wcsupr`, `_wcsupr_s`, `_wtoi`, `_wtoi64`, `atoi`, `memchr`, `strcat_s`, `strcmp`, `strcpy_s`, `strcspn`, `strlen`, `strncat_s`, `strncpy_s`, `strpbrk`, `strspn`, `strtok_s`, `strtol`, `strtoul`, `wcscat_s`, `wcschr`, `wcscmp`, `wcscpy_s`, `wcscspn`, `wcslen`, `wcsncat_s`, `wcsncmp`, `wcsncpy_s`, `wcspbrk`, `wcsrchr`, `wcsspn`, `wcstok_s`, `wcstol`, `wcstoul`

## Windows/System32/ntdll.dll — 68 functions
`RtlAnsiStringToUnicodeString`, `RtlAppendUnicodeToString`, `RtlAreBitsClear`, `RtlAreBitsSet`, `RtlCompareMemory`, `RtlCompareMemoryUlong`, `RtlCompareString`, `RtlCompareUnicodeString`, `RtlConvertSidToUnicodeString`, `RtlCrc64`, `RtlDowncaseUnicodeString`, `RtlEqualString`, `RtlEqualUnicodeString`, `RtlEthernetAddressToStringA`, `RtlEthernetAddressToStringW`, `RtlEthernetStringToAddressA`, `RtlEthernetStringToAddressW`, `RtlFindCharInUnicodeString`, `RtlFindLongestRunClear`, `RtlGUIDFromString`, `RtlHashUnicodeString`, `RtlInitString`, `RtlInitStringEx`, `RtlInitUnicodeString`, `RtlInitUnicodeStringEx`, `RtlInt64ToUnicodeString`, `RtlIntegerToChar`, `RtlIntegerToUnicodeString`, `RtlIpv4AddressToStringA`, `RtlIpv4AddressToStringExA`, `RtlIpv4AddressToStringExW`, `RtlIpv4AddressToStringW`, `RtlIpv4StringToAddressA`, `RtlIpv4StringToAddressExA`, `RtlIpv4StringToAddressExW`, `RtlIpv4StringToAddressW`, `RtlIpv6AddressToStringA`, `RtlIpv6AddressToStringExA`, `RtlIpv6AddressToStringExW`, `RtlIpv6AddressToStringW`, `RtlIpv6StringToAddressA`, `RtlIpv6StringToAddressExA`, `RtlIpv6StringToAddressW`, `RtlIsTextUnicode`, `RtlLargeIntegerToChar`, `RtlMultiByteToUnicodeN`, `RtlNumberOfClearBits`, `RtlNumberOfSetBits`, `RtlOemStringToUnicodeString`, `RtlOemToUnicodeN`, `RtlPrefixString`, `RtlPrefixUnicodeString`, `RtlSecondsSince1970ToTime`, `RtlStringFromGUIDEx`, `RtlTimeFieldsToTime`, `RtlTimeToTimeFields`, `RtlUTF8ToUnicodeN`, `RtlUdiv128`, `RtlUnicodeStringToAnsiString`, `RtlUnicodeStringToOemString`, `RtlUnicodeToMultiByteN`, `RtlUnicodeToOemN`, `RtlUnicodeToUTF8N`, `RtlUpcaseUnicodeString`, `RtlUpcaseUnicodeStringToAnsiString`, `RtlUpcaseUnicodeToMultiByteN`, `RtlUpcaseUnicodeToOemN`, `RtlUpperString`

## Windows/System32/msvcrt.dll — 35 functions
`_i64toa`, `_i64tow`, `_itoa`, `_itow`, `_memicmp`, `_stricmp`, `_strlwr`, `_strnicmp`, `_strnset`, `_strrev`, `_strset`, `_strupr`, `_ui64toa`, `_ui64tow`, `_ultoa`, `_ultow`, `_wcsicmp`, `_wcsnicmp`, `_wcsnset`, `_wcsrev`, `_wcsset`, `_wcsupr`, `memchr`, `strcmp`, `strcspn`, `strlen`, `strpbrk`, `strspn`, `wcschr`, `wcscmp`, `wcscspn`, `wcslen`, `wcsncmp`, `wcspbrk`, `wcsspn`

## Windows/System32/shlwapi.dll — 22 functions
`PathFindExtensionW`, `PathFindFileNameW`, `PathFindNextComponentW`, `PathIsFileSpecW`, `PathQuoteSpacesW`, `PathRemoveArgsW`, `PathRemoveBackslashW`, `PathRemoveBlanksW`, `PathRemoveExtensionW`, `PathRenameExtensionW`, `PathStripPathW`, `PathUndecorateW`, `StrCSpnW`, `StrCatBuffW`, `StrChrNW`, `StrChrW`, `StrCpyNW`, `StrPBrkW`, `StrRChrW`, `StrSpnW`, `StrStrW`, `StrTrimW`

## Windows/System32/crypt32.dll — 16 functions
`CryptBinaryToStringA`, `CryptBinaryToStringA`, `CryptBinaryToStringA`, `CryptBinaryToStringA`, `CryptBinaryToStringW`, `CryptBinaryToStringW`, `CryptBinaryToStringW`, `CryptBinaryToStringW`, `CryptStringToBinaryA`, `CryptStringToBinaryA`, `CryptStringToBinaryA`, `CryptStringToBinaryA`, `CryptStringToBinaryW`, `CryptStringToBinaryW`, `CryptStringToBinaryW`, `CryptStringToBinaryW`

## Windows/System32/kernelbase.dll — 6 functions
`PathCchAddBackslash`, `PathCchAddExtension`, `PathCchFindExtension`, `PathCchRemoveBackslash`, `PathCchRemoveExtension`, `PathCchRenameExtension`

## Windows/System32/iphlpapi.dll — 2 functions
`ConvertGuidToStringA`, `ConvertGuidToStringW`

## Windows/System32/rpcrt4.dll — 1 functions
`UuidFromStringA`

