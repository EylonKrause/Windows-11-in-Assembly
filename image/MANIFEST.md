# Image manifest — hand-ASM reimplementations mapped to the Win11 System32 tree

288 `.asm` files across 12 System32 DLL folders (materialized under `tree/Windows/System32/`).

## Windows/System32/ntdll.dll — 84 functions
`RtlAnsiStringToUnicodeString`, `RtlAppendAsciizToString`, `RtlAppendUnicodeToString`, `RtlAreBitsClear`, `RtlAreBitsSet`, `RtlCompareMemory`, `RtlCompareMemoryUlong`, `RtlCompareString`, `RtlCompareUnicodeString`, `RtlCompareUnicodeStrings`, `RtlConvertSidToUnicodeString`, `RtlCrc32`, `RtlCrc64`, `RtlDowncaseUnicodeString`, `RtlEqualString`, `RtlEqualUnicodeString`, `RtlEthernetAddressToStringA`, `RtlEthernetAddressToStringW`, `RtlEthernetStringToAddressA`, `RtlEthernetStringToAddressW`, `RtlFindCharInUnicodeString`, `RtlFindClearBits`, `RtlFindClearRuns`, `RtlFindLastBackwardRunClear`, `RtlFindLongestRunClear`, `RtlFindNextForwardRunClear`, `RtlFindSetBits`, `RtlFindUnicodeSubstring`, `RtlGUIDFromString`, `RtlHashUnicodeString`, `RtlInitString`, `RtlInitStringEx`, `RtlInitUTF8String`, `RtlInitUnicodeString`, `RtlInitUnicodeStringEx`, `RtlInt64ToUnicodeString`, `RtlIntegerToChar`, `RtlIntegerToUnicodeString`, `RtlIpv4AddressToStringA`, `RtlIpv4AddressToStringExA`, `RtlIpv4AddressToStringExW`, `RtlIpv4AddressToStringW`, `RtlIpv4StringToAddressA`, `RtlIpv4StringToAddressExA`, `RtlIpv4StringToAddressExW`, `RtlIpv4StringToAddressW`, `RtlIpv6AddressToStringA`, `RtlIpv6AddressToStringExA`, `RtlIpv6AddressToStringExW`, `RtlIpv6AddressToStringW`, `RtlIpv6StringToAddressA`, `RtlIpv6StringToAddressExA`, `RtlIpv6StringToAddressExW`, `RtlIpv6StringToAddressW`, `RtlIsTextUnicode`, `RtlIsZeroMemory`, `RtlLargeIntegerToChar`, `RtlMultiByteToUnicodeN`, `RtlNumberOfClearBits`, `RtlNumberOfClearBitsInRange`, `RtlNumberOfSetBits`, `RtlNumberOfSetBitsInRange`, `RtlOemStringToUnicodeString`, `RtlOemToUnicodeN`, `RtlPrefixString`, `RtlPrefixUnicodeString`, `RtlSecondsSince1970ToTime`, `RtlStringFromGUIDEx`, `RtlTimeFieldsToTime`, `RtlTimeToTimeFields`, `RtlUTF8StringToUnicodeString`, `RtlUTF8ToUnicodeN`, `RtlUdiv128`, `RtlUnicodeStringToAnsiString`, `RtlUnicodeStringToOemString`, `RtlUnicodeStringToUTF8String`, `RtlUnicodeToMultiByteN`, `RtlUnicodeToOemN`, `RtlUnicodeToUTF8N`, `RtlUpcaseUnicodeString`, `RtlUpcaseUnicodeStringToAnsiString`, `RtlUpcaseUnicodeToMultiByteN`, `RtlUpcaseUnicodeToOemN`, `RtlUpperString`

## Windows/System32/ucrtbase.dll — 75 functions
`_atoi64`, `_i64toa`, `_i64toa_s`, `_i64tow`, `_i64tow_s`, `_itoa`, `_itoa_s`, `_itow`, `_itow_s`, `_memccpy`, `_memicmp`, `_stricmp`, `_strlwr_s`, `_strnicmp`, `_strnset`, `_strnset_s`, `_strrev`, `_strset`, `_strset_s`, `_strtoi64`, `_strtoui64`, `_strupr`, `_strupr_s`, `_swab`, `_ui64toa`, `_ui64toa_s`, `_ui64tow`, `_ui64tow_s`, `_ultoa`, `_ultoa_s`, `_ultow`, `_ultow_s`, `_wcsicmp`, `_wcslwr_s`, `_wcsnicmp`, `_wcsnset`, `_wcsnset_s`, `_wcsrev`, `_wcsset`, `_wcsset_s`, `_wcstoi64`, `_wcstoui64`, `_wcsupr`, `_wcsupr_s`, `_wtoi`, `_wtoi64`, `atoi`, `memchr`, `strcat_s`, `strcmp`, `strcpy_s`, `strcspn`, `strlen`, `strncat_s`, `strncpy_s`, `strpbrk`, `strspn`, `strtok_s`, `strtol`, `strtoul`, `wcscat_s`, `wcschr`, `wcscmp`, `wcscpy_s`, `wcscspn`, `wcslen`, `wcsncat_s`, `wcsncmp`, `wcsncpy_s`, `wcspbrk`, `wcsrchr`, `wcsspn`, `wcstok_s`, `wcstol`, `wcstoul`

## Windows/System32/shlwapi.dll — 50 functions
`PathAddBackslashW`, `PathCommonPrefixA`, `PathCommonPrefixW`, `PathFindExtensionA`, `PathFindExtensionW`, `PathFindFileNameA`, `PathFindFileNameW`, `PathFindNextComponentA`, `PathFindNextComponentW`, `PathIsFileSpecA`, `PathIsFileSpecW`, `PathIsPrefixA`, `PathIsPrefixW`, `PathIsSameRootW`, `PathMakePrettyA`, `PathQuoteSpacesA`, `PathQuoteSpacesW`, `PathRemoveArgsA`, `PathRemoveArgsW`, `PathRemoveBackslashA`, `PathRemoveBackslashW`, `PathRemoveBlanksA`, `PathRemoveBlanksW`, `PathRemoveExtensionA`, `PathRemoveExtensionW`, `PathRenameExtensionA`, `PathRenameExtensionW`, `PathStripPathA`, `PathStripPathW`, `PathUndecorateA`, `PathUndecorateW`, `StrCSpnA`, `StrCSpnW`, `StrCatBuffA`, `StrCatBuffW`, `StrChrA`, `StrChrIW`, `StrChrNW`, `StrChrW`, `StrCpyNW`, `StrPBrkA`, `StrPBrkW`, `StrRChrA`, `StrRChrIW`, `StrRChrW`, `StrSpnA`, `StrSpnW`, `StrStrW`, `StrTrimA`, `StrTrimW`

## Windows/System32/msvcrt.dll — 34 functions
`_i64toa`, `_i64tow`, `_itoa`, `_itow`, `_memicmp`, `_stricmp`, `_strnicmp`, `_strnset`, `_strrev`, `_strset`, `_strupr`, `_ui64toa`, `_ui64tow`, `_ultoa`, `_ultow`, `_wcsicmp`, `_wcsnicmp`, `_wcsnset`, `_wcsrev`, `_wcsset`, `_wcsupr`, `memchr`, `strcmp`, `strcspn`, `strlen`, `strpbrk`, `strspn`, `wcschr`, `wcscmp`, `wcscspn`, `wcslen`, `wcsncmp`, `wcspbrk`, `wcsspn`

## Windows/System32/kernelbase.dll — 27 functions
`CompareStringOrdinal`, `FindStringOrdinal`, `HashData`, `PathAddExtensionW`, `PathCanonicalizeW`, `PathCchAddBackslash`, `PathCchAddBackslashEx`, `PathCchAddExtension`, `PathCchAppendEx`, `PathCchCanonicalizeEx`, `PathCchCombineEx`, `PathCchFindExtension`, `PathCchRemoveBackslash`, `PathCchRemoveBackslashEx`, `PathCchRemoveExtension`, `PathCchRemoveFileSpec`, `PathCchRenameExtension`, `UrlHashA`, `UrlUnescapeA`, `UrlUnescapeW`, `lstrcatA`, `lstrcatW`, `lstrcpyA`, `lstrcpyW`, `lstrcpynA`, `lstrcpynW`, `lstrlenA`

## Windows/System32/crypt32.dll — 4 functions
`CryptBinaryToStringA`, `CryptBinaryToStringW`, `CryptStringToBinaryA`, `CryptStringToBinaryW`

## Windows/System32/advapi32.dll — 4 functions
`ConvertSidToStringSidA`, `ConvertSidToStringSidW`, `ConvertStringSidToSidA`, `ConvertStringSidToSidW`

## Windows/System32/iphlpapi.dll — 2 functions
`ConvertGuidToStringA`, `ConvertGuidToStringW`

## Windows/System32/rpcrt4.dll — 2 functions
`UuidFromStringA`, `UuidFromStringW`

## Windows/System32/combase.dll — 2 functions
`IIDFromString`, `StringFromGUID2`

## Windows/System32/ws2_32.dll — 2 functions
`inet_addr`, `inet_ntoa`

## Windows/System32/user32.dll — 2 functions
`CharLowerBuffW`, `CharUpperBuffW`

