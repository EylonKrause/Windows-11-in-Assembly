# The desktop's actual call surface, and the part of it we have

Built by `tools/desktop-surface.py --profile desktop` from the **running** system on this machine: the modules currently mapped into explorer.exe, dwm.exe, SearchHost, StartMenuExperienceHost, sihost, ShellHost, TextInputHost, ApplicationFrameHost, RuntimeBroker, taskhostw and ctfmon, and the union of their **import tables**.

An export is a function that exists. An import is a function something actually binds to. This is the second thing.

## Totals

| | |
|---|---:|
| desktop modules read | **459** |
| distinct (dll, function) bindings | **10530** |
| already converted in `image/tree` | **360** |
| excluded as known-not-a-target | **3730** |
| **remaining candidates** | **6440** |
| of those, string/path/bit shaped | **1201** |

The excluded count is not a rounding error and the reasons matter, so they are listed rather than applied silently:

| excluded because | count |
|---|---:|
| syscall stub: the body is a syscall | 1478 |
| C++ mangled, ABI-unstable | 828 |
| ordinal forwarder | 313 |
| allocator/VM: paid in the lock or the kernel | 305 |
| synchronisation primitive | 130 |
| CRT internal plumbing | 129 |
| registry: paid in the kernel transition | 123 |
| RPC marshalling | 119 |
| kernel object call: paid in the transition | 102 |
| COM plumbing | 62 |
| loader internals | 54 |
| NLS/collation: needs the OS tables to be bit-exact | 50 |
| ETW tracing | 35 |
| code-page dependent | 2 |

## Ranked by fan-in — how many of the desktop's own modules bind it

Fan-in is a proxy for pervasiveness, not for time spent. A routine bound by two hundred of this profile's DLLs pays back everywhere at once if it is beatable; it is still only a candidate until it is timed against the live export.

| # | dll | function | desktop modules importing it | shaped like a target |
|---:|---|---|---:|:--:|
| 1 | `api-ms-win-core-processthreads-l1-1-0.dll` | `GetCurrentProcessId` | **396** |  |
| 2 | `api-ms-win-core-processthreads-l1-1-0.dll` | `GetCurrentThreadId` | **394** |  |
| 3 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetSystemTimeAsFileTime` | **393** |  |
| 4 | `api-ms-win-core-profile-l1-1-0.dll` | `QueryPerformanceCounter` | **391** |  |
| 5 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `GetLastError` | **376** |  |
| 6 | `api-ms-win-core-processthreads-l1-1-0.dll` | `GetCurrentProcess` | **372** |  |
| 7 | `api-ms-win-core-processthreads-l1-1-0.dll` | `TerminateProcess` | **359** |  |
| 8 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `SetUnhandledExceptionFilter` | **357** |  |
| 9 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `UnhandledExceptionFilter` | **356** |  |
| 10 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `SetLastError` | **345** |  |
| 11 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetProcAddress` | **344** |  |
| 12 | `api-ms-win-core-debug-l1-1-0.dll` | `IsDebuggerPresent` | **340** |  |
| 13 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleHandleW` | **323** |  |
| 14 | `api-ms-win-core-localization-l1-2-0.dll` | `FormatMessageW` | **320** | yes |
| 15 | `api-ms-win-core-delayload-l1-1-0.dll` | `DelayLoadFailureHook` | **315** |  |
| 16 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleHandleExW` | **312** |  |
| 17 | `api-ms-win-core-string-l1-1-0.dll` | `WideCharToMultiByte` | **310** | yes |
| 18 | `api-ms-win-core-delayload-l1-1-1.dll` | `ResolveDelayLoadedAPI` | **309** |  |
| 19 | `api-ms-win-core-synch-l1-1-0.dll` | `AcquireSRWLockExclusive` | **308** |  |
| 20 | `api-ms-win-core-synch-l1-1-0.dll` | `ReleaseSRWLockExclusive` | **308** |  |
| 21 | `api-ms-win-core-debug-l1-1-0.dll` | `OutputDebugStringW` | **304** | yes |
| 22 | `api-ms-win-core-debug-l1-1-0.dll` | `DebugBreak` | **299** |  |
| 23 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetTickCount` | **298** |  |
| 24 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleFileNameA` | **294** |  |
| 25 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `DisableThreadLibraryCalls` | **291** |  |
| 26 | `api-ms-win-crt-runtime-l1-1-0.dll` | `_initterm` | **285** |  |
| 27 | `api-ms-win-crt-runtime-l1-1-0.dll` | `_initterm_e` | **285** |  |
| 28 | `api-ms-win-core-synch-l1-1-0.dll` | `AcquireSRWLockShared` | **279** |  |
| 29 | `api-ms-win-core-synch-l1-1-0.dll` | `ReleaseSRWLockShared` | **279** |  |
| 30 | `api-ms-win-core-threadpool-l1-2-0.dll` | `SetThreadpoolTimer` | **274** |  |
| 31 | `api-ms-win-core-interlocked-l1-1-0.dll` | `InitializeSListHead` | **270** |  |
| 32 | `api-ms-win-core-rtlsupport-l1-1-0.dll` | `RtlLookupFunctionEntry` | **268** |  |
| 33 | `api-ms-win-core-rtlsupport-l1-1-0.dll` | `RtlCaptureContext` | **267** |  |
| 34 | `api-ms-win-core-rtlsupport-l1-1-0.dll` | `RtlVirtualUnwind` | **267** |  |
| 35 | `api-ms-win-core-processthreads-l1-1-1.dll` | `IsProcessorFeaturePresent` | **248** |  |
| 36 | `api-ms-win-crt-string-l1-1-0.dll` | `memset` | **247** | yes |
| 37 | `api-ms-win-core-synch-l1-1-0.dll` | `CreateEventW` | **242** |  |
| 38 | `api-ms-win-crt-private-l1-1-0.dll` | `memcpy` | **236** | yes |
| 39 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `RaiseException` | **227** |  |
| 40 | `api-ms-win-core-synch-l1-2-0.dll` | `Sleep` | **222** |  |
| 41 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `LoadLibraryExW` | **215** |  |
| 42 | `api-ms-win-crt-private-l1-1-0.dll` | `memcmp` | **212** | yes |
| 43 | `api-ms-win-core-string-l1-1-0.dll` | `MultiByteToWideChar` | **211** | yes |
| 44 | `api-ms-win-crt-private-l1-1-0.dll` | `memmove` | **211** | yes |
| 45 | `api-ms-win-core-synch-l1-2-0.dll` | `InitOnceBeginInitialize` | **202** |  |
| 46 | `api-ms-win-core-synch-l1-2-0.dll` | `InitOnceComplete` | **202** |  |
| 47 | `api-ms-win-core-synch-l1-2-0.dll` | `InitOnceExecuteOnce` | **200** |  |
| 48 | `api-ms-win-crt-private-l1-1-0.dll` | `_CxxThrowException` | **200** |  |
| 49 | `api-ms-win-core-processthreads-l1-1-0.dll` | `GetCurrentThread` | **186** |  |
| 50 | `api-ms-win-core-util-l1-1-0.dll` | `EncodePointer` | **175** | yes |
| 51 | `api-ms-win-security-base-l1-1-0.dll` | `GetTokenInformation` | **174** | yes |
| 52 | `api-ms-win-core-util-l1-1-0.dll` | `DecodePointer` | **166** | yes |
| 53 | `api-ms-win-core-apiquery-l1-1-0.dll` | `ApiSetQueryApiSetPresence` | **164** |  |
| 54 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleFileNameW` | **154** |  |
| 55 | `api-ms-win-core-synch-l1-1-0.dll` | `InitializeSRWLock` | **153** |  |
| 56 | `ntdll.dll` | `RtlNtStatusToDosError` | **143** |  |
| 57 | `api-ms-win-core-synch-l1-1-0.dll` | `CreateEventExW` | **136** |  |
| 58 | `api-ms-win-eventing-provider-l1-1-0.dll` | `EventActivityIdControl` | **134** |  |
| 59 | `api-ms-win-core-processenvironment-l1-1-0.dll` | `ExpandEnvironmentStringsW` | **131** | yes |
| 60 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleHandleExA` | **120** |  |
| 61 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetTickCount64` | **115** |  |
| 62 | `api-ms-win-core-winrt-string-l1-1-0.dll` | `WindowsCreateStringReference` | **113** | yes |
| 63 | `api-ms-win-core-winrt-l1-1-0.dll` | `RoGetActivationFactory` | **112** |  |
| 64 | `ntdll.dll` | `RtlCaptureContext` | **111** |  |
| 65 | `ntdll.dll` | `RtlLookupFunctionEntry` | **111** |  |
| 66 | `ntdll.dll` | `RtlVirtualUnwind` | **111** |  |
| 67 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `LoadStringW` | **109** | yes |
| 68 | `api-ms-win-core-winrt-string-l1-1-0.dll` | `WindowsGetStringRawBuffer` | **109** | yes |
| 69 | `api-ms-win-core-interlocked-l1-1-0.dll` | `InterlockedPushEntrySList` | **108** |  |
| 70 | `api-ms-win-core-winrt-error-l1-1-0.dll` | `RoOriginateError` | **105** |  |
| 71 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `LoadResource` | **104** |  |
| 72 | `api-ms-win-core-winrt-string-l1-1-0.dll` | `WindowsDeleteString` | **104** | yes |
| 73 | `msvcrt.dll` | `_XcptFilter` | **103** |  |
| 74 | `msvcrt.dll` | `_initterm` | **103** |  |
| 75 | `msvcrt.dll` | `free` | **103** |  |
| 76 | `msvcrt.dll` | `malloc` | **103** |  |
| 77 | `ntdll.dll` | `RtlSubscribeWnfStateChangeNotification` | **101** |  |
| 78 | `msvcrt.dll` | `memset` | **97** | yes |
| 79 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `LockResource` | **93** |  |
| 80 | `api-ms-win-core-libraryloader-l1-2-1.dll` | `LoadLibraryW` | **93** |  |
| 81 | `api-ms-win-core-string-l1-1-0.dll` | `CompareStringW` | **93** | yes |
| 82 | `api-ms-win-core-winrt-string-l1-1-0.dll` | `WindowsCreateString` | **92** | yes |
| 83 | `msvcrt.dll` | `memcpy` | **89** | yes |
| 84 | `api-ms-win-core-memory-l1-1-0.dll` | `UnmapViewOfFile` | **87** |  |
| 85 | `api-ms-win-core-file-l1-1-0.dll` | `GetFileAttributesW` | **84** |  |
| 86 | `api-ms-win-security-base-l1-1-0.dll` | `GetLengthSid` | **84** | yes |
| 87 | `api-ms-win-core-file-l1-1-0.dll` | `FindClose` | **83** | yes |
| 88 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetSystemDirectoryW` | **82** |  |
| 89 | `api-ms-win-core-threadpool-l1-2-0.dll` | `SetThreadpoolWait` | **82** |  |
| 90 | `api-ms-win-core-winrt-string-l1-1-0.dll` | `WindowsIsStringEmpty` | **82** | yes |
| 91 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `FindResourceExW` | **81** | yes |
| 92 | `msvcrt.dll` | `_lock` | **81** |  |
| 93 | `msvcrt.dll` | `_onexit` | **81** |  |
| 94 | `msvcrt.dll` | `_unlock` | **81** |  |
| 95 | `ntdll.dll` | `RtlUnsubscribeWnfStateChangeNotification` | **81** |  |
| 96 | `msvcrt.dll` | `_vsnwprintf` | **80** |  |
| 97 | `ntdll.dll` | `RtlGetDeviceFamilyInfoEnum` | **79** |  |
| 98 | `api-ms-win-core-memory-l1-1-0.dll` | `MapViewOfFile` | **78** |  |
| 99 | `api-ms-win-core-processthreads-l1-1-0.dll` | `TlsSetValue` | **78** |  |
| 100 | `api-ms-win-core-com-l1-1-0.dll` | `PropVariantClear` | **77** |  |

## The shaped candidates, by DLL

### api-ms-win-security-base-l1-1-0.dll — 28 candidates

```
  GetTokenInformation                          fan-in 174
  GetLengthSid                                 fan-in 84
  CopySid                                      fan-in 62
  RevertToSelf                                 fan-in 59
  EqualSid                                     fan-in 55
  CreateWellKnownSid                           fan-in 54
  DuplicateTokenEx                             fan-in 52
  IsValidSid                                   fan-in 52
  CheckTokenMembership                         fan-in 49
  GetSidSubAuthority                           fan-in 41
  GetSidSubAuthorityCount                      fan-in 29
  DuplicateToken                               fan-in 27
  AdjustTokenPrivileges                        fan-in 25
  IsWellKnownSid                               fan-in 20
  GetSecurityDescriptorLength                  fan-in 14
  GetSidLengthRequired                         fan-in 13
  InitializeSid                                fan-in 13
  GetSidIdentifierAuthority                    fan-in 9
  SetTokenInformation                          fan-in 9
  DestroyPrivateObjectSecurity                 fan-in 8
  CreateRestrictedToken                        fan-in 6
  EqualDomainSid                               fan-in 4
  ImpersonateAnonymousToken                    fan-in 3
  AdjustTokenGroups                            fan-in 2
  ConvertToAutoInheritPrivateObjectSecurity    fan-in 2
  EqualPrefixSid                               fan-in 2
  IsTokenRestricted                            fan-in 2
  GetWindowsAccountDomainSid                   fan-in 1
```

### ntdll.dll — 164 candidates

```
  RtlLengthSid                                 fan-in 41
  RtlCopySid                                   fan-in 28
  RtlEqualSid                                  fan-in 28
  RtlInitAnsiString                            fan-in 24
  memset                                       fan-in 24
  RtlUpcaseUnicodeChar                         fan-in 23
  RtlValidSid                                  fan-in 23
  memcpy                                       fan-in 23
  RtlCopyUnicodeString                         fan-in 20
  WinSqmAddToStream                            fan-in 20
  RtlAppendUnicodeStringToString               fan-in 19
  RtlDosPathNameToNtPathName_U                 fan-in 17
  memcmp                                       fan-in 17
  RtlDosPathNameToNtPathName_U_WithStatus      fan-in 15
  RtlUnicodeStringToInteger                    fan-in 15
  memmove                                      fan-in 15
  wcsstr                                       fan-in 14
  RtlDeriveCapabilitySidsFromName              fan-in 13
  RtlFormatCurrentUserKeyPath                  fan-in 13
  RtlCreateUnicodeString                       fan-in 12
  RtlExpandEnvironmentStrings                  fan-in 12
  WinSqmAddToStreamEx                          fan-in 12
  strchr                                       fan-in 12
  RtlGetAppContainerSidType                    fan-in 11
  RtlSubAuthoritySid                           fan-in 11
  RtlxAnsiStringToUnicodeSize                  fan-in 11
  RtlStringFromGUID                            fan-in 10
  RtlSubAuthorityCountSid                      fan-in 10
  RtlCreateUnicodeStringFromAsciiz             fan-in 9
  RtlInitializeSid                             fan-in 9
  RtlLengthSecurityDescriptor                  fan-in 9
  memmove_s                                    fan-in 9
  RtlDosPathNameToRelativeNtPathName_U_WithStatus fan-in 8
  RtlLengthRequiredSid                         fan-in 8
  RtlQueryTokenHostIdAsUlong64                 fan-in 8
  strrchr                                      fan-in 8
  RtlCreateServiceSid                          fan-in 7
  RtlDetermineDosPathNameType_U                fan-in 7
  RtlDosPathNameToRelativeNtPathName_U         fan-in 7
  RtlNtPathNameToDosPathName                   fan-in 7
  strncmp                                      fan-in 7
  wcsnlen                                      fan-in 7
  RtlDestroyEnvironment                        fan-in 6
  RtlDuplicateUnicodeString                    fan-in 6
  RtlGetFullPathName_UEx                       fan-in 6
  RtlLoadString                                fan-in 6
  RtlxUnicodeStringToAnsiSize                  fan-in 6
  RtlComputeCrc32                              fan-in 5
  RtlGetAppContainerNamedObjectPath            fan-in 5
  RtlGetFullPathName_U                         fan-in 5
  memcpy_s                                     fan-in 5
  RtlDestroyQueryDebugBuffer                   fan-in 4
  RtlExpandEnvironmentStrings_U                fan-in 4
  RtlGetTokenNamedObjectPath                   fan-in 4
  RtlInitAnsiStringEx                          fan-in 4
  RtlRunEncodeUnicodeString                    fan-in 4
  RtlxUnicodeStringToOemSize                   fan-in 4
  _wcslwr                                      fan-in 4
  RtlAnsiCharToUnicodeChar                     fan-in 3
  RtlCheckTokenCapability                      fan-in 3
  ... and 104 more
```

### api-ms-win-crt-private-l1-1-0.dll — 7 candidates

```
  memcpy                                       fan-in 236
  memcmp                                       fan-in 212
  memmove                                      fan-in 211
  wcsstr                                       fan-in 65
  strchr                                       fan-in 56
  strrchr                                      fan-in 18
  strstr                                       fan-in 10
```

### api-ms-win-core-winrt-string-l1-1-0.dll — 18 candidates

```
  WindowsCreateStringReference                 fan-in 113
  WindowsGetStringRawBuffer                    fan-in 109
  WindowsDeleteString                          fan-in 104
  WindowsCreateString                          fan-in 92
  WindowsIsStringEmpty                         fan-in 82
  WindowsDuplicateString                       fan-in 69
  WindowsStringHasEmbeddedNull                 fan-in 68
  WindowsCompareStringOrdinal                  fan-in 45
  WindowsGetStringLen                          fan-in 31
  WindowsConcatString                          fan-in 22
  WindowsSubstringWithSpecifiedLength          fan-in 19
  WindowsDeleteStringBuffer                    fan-in 14
  WindowsPreallocateStringBuffer               fan-in 14
  WindowsPromoteStringBuffer                   fan-in 14
  WindowsReplaceString                         fan-in 2
  WindowsTrimStringEnd                         fan-in 2
  WindowsTrimStringStart                       fan-in 2
  WindowsSubstring                             fan-in 1
```

### api-ms-win-core-string-l1-1-0.dll — 5 candidates

```
  WideCharToMultiByte                          fan-in 310
  MultiByteToWideChar                          fan-in 211
  CompareStringW                               fan-in 93
  CompareStringEx                              fan-in 23
  GetStringTypeExW                             fan-in 12
```

### msvcrt.dll — 24 candidates

```
  memset                                       fan-in 97
  memcpy                                       fan-in 89
  memcpy_s                                     fan-in 77
  memcmp                                       fan-in 67
  memmove                                      fan-in 64
  memmove_s                                    fan-in 56
  wcsstr                                       fan-in 30
  wcsnlen                                      fan-in 12
  _wcsdup                                      fan-in 8
  strchr                                       fan-in 8
  _wcslwr                                      fan-in 7
  strstr                                       fan-in 7
  strrchr                                      fan-in 5
  strncmp                                      fan-in 4
  _strlwr                                      fan-in 3
  wcstod                                       fan-in 3
  _strdup                                      fan-in 2
  strnlen                                      fan-in 2
  wcstombs                                     fan-in 2
  wcstombs_s                                   fan-in 2
  _Strftime                                    fan-in 1
  mbstowcs                                     fan-in 1
  wcscoll                                      fan-in 1
  wcstok                                       fan-in 1
```

### api-ms-win-core-file-l1-1-0.dll — 26 candidates

```
  FindClose                                    fan-in 83
  FindFirstFileW                               fan-in 76
  FindNextFileW                                fan-in 69
  CompareFileTime                              fan-in 53
  GetFullPathNameW                             fan-in 49
  FindFirstFileExW                             fan-in 21
  GetFinalPathNameByHandleW                    fan-in 21
  GetVolumePathNameW                           fan-in 21
  GetLongPathNameW                             fan-in 18
  GetFullPathNameA                             fan-in 12
  GetShortPathNameW                            fan-in 9
  FindCloseChangeNotification                  fan-in 4
  FindFirstChangeNotificationW                 fan-in 4
  FindFirstFileA                               fan-in 4
  FindFirstVolumeW                             fan-in 4
  FindNextChangeNotification                   fan-in 4
  FindNextFileA                                fan-in 4
  FindNextVolumeW                              fan-in 4
  FindVolumeClose                              fan-in 4
  FindFirstFileNameW                           fan-in 3
  FindNextFileNameW                            fan-in 3
  GetLogicalDriveStringsW                      fan-in 3
  GetFinalPathNameByHandleA                    fan-in 2
  FindFirstChangeNotificationA                 fan-in 1
  FindFirstFileExA                             fan-in 1
  GetLongPathNameA                             fan-in 1
```

### kernel32.dll — 72 candidates

```
  MultiByteToWideChar                          fan-in 29
  WideCharToMultiByte                          fan-in 28
  EncodePointer                                fan-in 25
  OutputDebugStringW                           fan-in 25
  FormatMessageW                               fan-in 23
  FindClose                                    fan-in 20
  FindNextFileW                                fan-in 17
  LCMapStringW                                 fan-in 16
  GetEnvironmentStringsW                       fan-in 15
  FindFirstFileExW                             fan-in 14
  CompareStringW                               fan-in 13
  DecodePointer                                fan-in 13
  OutputDebugStringA                           fan-in 13
  lstrcmpiW                                    fan-in 13
  FindFirstFileW                               fan-in 12
  ExpandEnvironmentStringsW                    fan-in 11
  FindResourceExW                              fan-in 10
  GetDateFormatW                               fan-in 10
  GetTimeFormatW                               fan-in 10
  GetFullPathNameW                             fan-in 9
  CompareStringEx                              fan-in 7
  FormatMessageA                               fan-in 7
  LCMapStringEx                                fan-in 7
  lstrlenW                                     fan-in 7
  FlushInstructionCache                        fan-in 6
  GetLongPathNameW                             fan-in 6
  CompareFileTime                              fan-in 5
  GetFinalPathNameByHandleW                    fan-in 5
  GetTempPathW                                 fan-in 5
  SearchPathW                                  fan-in 5
  lstrcmpW                                     fan-in 5
  FindActCtxSectionStringW                     fan-in 4
  CopyFileExW                                  fan-in 3
  CopyFileW                                    fan-in 3
  FindResourceW                                fan-in 3
  GetDateFormatEx                              fan-in 3
  GetTimeFormatEx                              fan-in 3
  EnumDateFormatsExEx                          fan-in 2
  GetCurrencyFormatEx                          fan-in 2
  GetNumberFormatEx                            fan-in 2
  GetNumberFormatW                             fan-in 2
  GetShortPathNameW                            fan-in 2
  GetStringTypeExW                             fan-in 2
  GetVolumePathNameW                           fan-in 2
  LZCopy                                       fan-in 2
  WritePrivateProfileStringW                   fan-in 2
  lstrcmpA                                     fan-in 2
  lstrcmpiA                                    fan-in 2
  CompareStringA                               fan-in 1
  ConvertNLSDayOfWeekToWin32DayOfWeek          fan-in 1
  CopyContext                                  fan-in 1
  EnumTimeFormatsEx                            fan-in 1
  EnumTimeFormatsW                             fan-in 1
  ExpandEnvironmentStringsA                    fan-in 1
  FindActCtxSectionGuid                        fan-in 1
  FindFirstChangeNotificationW                 fan-in 1
  FindFirstFileExA                             fan-in 1
  FindFirstFileNameW                           fan-in 1
  FindNLSStringEx                              fan-in 1
  FindNextFileA                                fan-in 1
  ... and 12 more
```

### api-ms-win-core-localization-l1-2-0.dll — 9 candidates

```
  FormatMessageW                               fan-in 320
  LCMapStringW                                 fan-in 39
  FormatMessageA                               fan-in 33
  LCMapStringEx                                fan-in 15
  FindNLSStringEx                              fan-in 13
  FindNLSString                                fan-in 12
  GetFileMUIPath                               fan-in 4
  LCMapStringA                                 fan-in 2
  IsNLSDefinedString                           fan-in 1
```

### api-ms-win-crt-string-l1-1-0.dll — 9 candidates

```
  memset                                       fan-in 247
  wcsnlen                                      fan-in 46
  strncmp                                      fan-in 39
  strnlen                                      fan-in 16
  memmove_s                                    fan-in 15
  _wcsdup                                      fan-in 5
  strncpy                                      fan-in 2
  wcsncpy                                      fan-in 2
  strcpy                                       fan-in 1
```

### api-ms-win-core-debug-l1-1-0.dll — 2 candidates

```
  OutputDebugStringW                           fan-in 304
  OutputDebugStringA                           fan-in 44
```

### api-ms-win-core-util-l1-1-0.dll — 2 candidates

```
  EncodePointer                                fan-in 175
  DecodePointer                                fan-in 166
```

### api-ms-win-core-shlwapi-legacy-l1-1-0.dll — 59 candidates

```
  PathFileExistsW                              fan-in 33
  PathIsRelativeW                              fan-in 16
  PathRemoveFileSpecW                          fan-in 15
  PathGetDriveNumberW                          fan-in 13
  PathIsUNCW                                   fan-in 13
  PathParseIconLocationW                       fan-in 13
  SHExpandEnvironmentStringsW                  fan-in 13
  PathAppendW                                  fan-in 9
  PathSkipRootW                                fan-in 8
  PathUnExpandEnvStringsW                      fan-in 8
  PathCombineW                                 fan-in 7
  PathStripToRootW                             fan-in 7
  PathIsRootW                                  fan-in 6
  PathUnquoteSpacesW                           fan-in 6
  PathGetArgsW                                 fan-in 5
  PathMatchSpecExW                             fan-in 5
  PathIsUNCServerShareW                        fan-in 4
  PathIsUNCServerW                             fan-in 4
  PathIsValidCharW                             fan-in 4
  IsCharSpaceW                                 fan-in 3
  PathMatchSpecW                               fan-in 3
  PathRelativePathToW                          fan-in 3
  PathAddBackslashA                            fan-in 2
  PathAppendA                                  fan-in 2
  PathFileExistsA                              fan-in 2
  PathGetArgsA                                 fan-in 2
  PathGetCharTypeW                             fan-in 2
  PathGetDriveNumberA                          fan-in 2
  PathIsRootA                                  fan-in 2
  PathRemoveFileSpecA                          fan-in 2
  PathSearchAndQualifyW                        fan-in 2
  PathUnExpandEnvStringsA                      fan-in 2
  SHExpandEnvironmentStringsA                  fan-in 2
  IsCharBlankW                                 fan-in 1
  IsCharCntrlW                                 fan-in 1
  IsCharDigitW                                 fan-in 1
  IsCharPunctW                                 fan-in 1
  IsCharSpaceA                                 fan-in 1
  IsCharXDigitW                                fan-in 1
  PathAddExtensionA                            fan-in 1
  PathCanonicalizeA                            fan-in 1
  PathCombineA                                 fan-in 1
  PathGetCharTypeA                             fan-in 1
  PathIsLFNFileSpecA                           fan-in 1
  PathIsLFNFileSpecW                           fan-in 1
  PathIsRelativeA                              fan-in 1
  PathIsSameRootA                              fan-in 1
  PathIsUNCA                                   fan-in 1
  PathIsUNCServerA                             fan-in 1
  PathIsUNCServerShareA                        fan-in 1
  PathMatchSpecA                               fan-in 1
  PathMatchSpecExA                             fan-in 1
  PathParseIconLocationA                       fan-in 1
  PathRelativePathToA                          fan-in 1
  PathSearchAndQualifyA                        fan-in 1
  PathSkipRootA                                fan-in 1
  PathStripToRootA                             fan-in 1
  PathUnquoteSpacesA                           fan-in 1
  SHTruncateString                             fan-in 1
```

### api-ms-win-core-shlwapi-obsolete-l1-1-0.dll — 37 candidates

```
  QISearch                                     fan-in 30
  StrCmpICW                                    fan-in 17
  StrCmpIW                                     fan-in 16
  StrCmpNICW                                   fan-in 12
  StrCmpNIW                                    fan-in 11
  StrCmpW                                      fan-in 11
  StrCmpCW                                     fan-in 9
  StrCmpNW                                     fan-in 9
  StrDupW                                      fan-in 9
  StrToIntW                                    fan-in 9
  StrCmpICA                                    fan-in 8
  StrCmpLogicalW                               fan-in 8
  StrToIntExW                                  fan-in 7
  StrStrA                                      fan-in 6
  StrDupA                                      fan-in 5
  StrStrIA                                     fan-in 5
  StrStrNIW                                    fan-in 5
  StrToIntA                                    fan-in 5
  StrCmpNA                                     fan-in 4
  StrCmpNCW                                    fan-in 4
  StrCmpNIA                                    fan-in 4
  StrCmpNICA                                   fan-in 4
  StrCmpNCA                                    fan-in 3
  StrToInt64ExW                                fan-in 3
  StrChrIA                                     fan-in 2
  StrCmpCA                                     fan-in 2
  StrCpyNXW                                    fan-in 2
  StrIsIntlEqualW                              fan-in 2
  StrRChrIA                                    fan-in 2
  StrRStrIA                                    fan-in 2
  StrCSpnIA                                    fan-in 1
  StrCatChainW                                 fan-in 1
  StrCpyNXA                                    fan-in 1
  StrIsIntlEqualA                              fan-in 1
  StrStrNW                                     fan-in 1
  StrToInt64ExA                                fan-in 1
  StrToIntExA                                  fan-in 1
```

### api-ms-win-core-libraryloader-l1-2-0.dll — 3 candidates

```
  LoadStringW                                  fan-in 109
  FindResourceExW                              fan-in 81
  LoadStringA                                  fan-in 15
```

### user32.dll — 48 candidates

```
  DestroyIcon                                  fan-in 20
  DestroyWindow                                fan-in 16
  DestroyMenu                                  fan-in 15
  FindWindowW                                  fan-in 12
  FillRect                                     fan-in 11
  CopyImage                                    fan-in 9
  CopyIcon                                     fan-in 7
  CopyRect                                     fan-in 7
  LoadStringW                                  fan-in 7
  AreDpiAwarenessContextsEqual                 fan-in 6
  CharNextW                                    fan-in 6
  EqualRect                                    fan-in 6
  GetWindowTextLengthW                         fan-in 6
  RegisterClipboardFormatW                     fan-in 6
  FindWindowExW                                fan-in 4
  GetMenuStringW                               fan-in 4
  IsClipboardFormatAvailable                   fan-in 4
  ValidateRect                                 fan-in 4
  CharLowerW                                   fan-in 3
  DestroyAcceleratorTable                      fan-in 3
  LoadBitmapW                                  fan-in 3
  CharPrevW                                    fan-in 2
  CharUpperW                                   fan-in 2
  CopyAcceleratorTableW                        fan-in 2
  CountClipboardFormats                        fan-in 2
  DestroyCaret                                 fan-in 2
  AddClipboardFormatListener                   fan-in 1
  CharNextA                                    fan-in 1
  CharToOemBuffW                               fan-in 1
  CharUpperA                                   fan-in 1
  DdeCreateStringHandleW                       fan-in 1
  DdeQueryConvInfo                             fan-in 1
  DdeQueryStringW                              fan-in 1
  DestroyCursor                                fan-in 1
  DestroyReasons                               fan-in 1
  EnumClipboardFormats                         fan-in 1
  GetClipboardFormatNameA                      fan-in 1
  GetClipboardFormatNameW                      fan-in 1
  GrayStringW                                  fan-in 1
  IsCharAlphaNumericW                          fan-in 1
  IsCharLowerW                                 fan-in 1
  IsServerSideWindow                           fan-in 1
  IsWindowInDestroy                            fan-in 1
  OemToCharBuffW                               fan-in 1
  RegisterClipboardFormatA                     fan-in 1
  RemoveClipboardFormatListener                fan-in 1
  SetMenuItemBitmaps                           fan-in 1
  ShutdownBlockReasonDestroy                   fan-in 1
```

### api-ms-win-core-string-obsolete-l1-1-0.dll — 5 candidates

```
  lstrcmpiW                                    fan-in 56
  lstrcmpW                                     fan-in 47
  lstrlenW                                     fan-in 45
  lstrcmpA                                     fan-in 17
  lstrcmpiA                                    fan-in 12
```

### api-ms-win-core-processenvironment-l1-1-0.dll — 6 candidates

```
  ExpandEnvironmentStringsW                    fan-in 131
  SearchPathW                                  fan-in 22
  ExpandEnvironmentStringsA                    fan-in 10
  GetEnvironmentStringsW                       fan-in 7
  GetEnvironmentStrings                        fan-in 1
  SetEnvironmentStringsW                       fan-in 1
```

### gdi32.dll — 66 candidates

```
  GetDIBits                                    fan-in 14
  SetStretchBltMode                            fan-in 11
  StretchBlt                                   fan-in 11
  CreateCompatibleBitmap                       fan-in 10
  StretchDIBits                                fan-in 9
  CreateBitmap                                 fan-in 6
  SetDIBits                                    fan-in 5
  CreateDIBitmap                               fan-in 4
  BeginPath                                    fan-in 3
  EndPath                                      fan-in 3
  Escape                                       fan-in 3
  FillRgn                                      fan-in 3
  GetBitmapBits                                fan-in 3
  GetTextCharset                               fan-in 3
  GetTextCharsetInfo                           fan-in 3
  SelectClipPath                               fan-in 3
  SetMetaFileBitsEx                            fan-in 3
  TranslateCharsetInfo                         fan-in 3
  AbortPath                                    fan-in 2
  CopyEnhMetaFileW                             fan-in 2
  CopyMetaFileW                                fan-in 2
  ExtEscape                                    fan-in 2
  FillPath                                     fan-in 2
  GdiGetCharDimensions                         fan-in 2
  GdiGradientFill                              fan-in 2
  GetCharABCWidthsW                            fan-in 2
  GetCharWidthInfo                             fan-in 2
  GetEnhMetaFileBits                           fan-in 2
  GetMetaFileBitsEx                            fan-in 2
  GetWinMetaFileBits                           fan-in 2
  SetBitmapBits                                fan-in 2
  SetEnhMetaFileBits                           fan-in 2
  SetPolyFillMode                              fan-in 2
  StrokeAndFillPath                            fan-in 2
  StrokePath                                   fan-in 2
  ClearBitmapAttributes                        fan-in 1
  CopyEnhMetaFileA                             fan-in 1
  CopyMetaFileA                                fan-in 1
  CreateBitmapFromDxSurface                    fan-in 1
  CreateBitmapIndirect                         fan-in 1
  D3DKMTEscape                                 fan-in 1
  DrawEscape                                   fan-in 1
  EqualRgn                                     fan-in 1
  FlattenPath                                  fan-in 1
  GdiConvertAndCheckDC                         fan-in 1
  GdiConvertBitmapV5                           fan-in 1
  GdiConvertEnhMetaFile                        fan-in 1
  GdiConvertMetaFilePict                       fan-in 1
  GdiConvertToDevmodeW                         fan-in 1
  GdiDrawStream                                fan-in 1
  GdiGetBitmapBitsSize                         fan-in 1
  GdiValidateHandle                            fan-in 1
  GetBitmapDimensionEx                         fan-in 1
  GetCharABCWidthsA                            fan-in 1
  GetCharWidth32W                              fan-in 1
  GetCharWidthA                                fan-in 1
  GetCharWidthW                                fan-in 1
  GetPath                                      fan-in 1
  GetPolyFillMode                              fan-in 1
  GetTextCharacterExtra                        fan-in 1
  ... and 6 more
```

### vcruntime140_app.dll — 7 candidates

```
  memcpy                                       fan-in 32
  memset                                       fan-in 31
  memmove                                      fan-in 29
  memcmp                                       fan-in 24
  strchr                                       fan-in 18
  wcsstr                                       fan-in 8
  strrchr                                      fan-in 1
```

### shlwapi.dll — 56 candidates

```
  SHStrDupW                                    fan-in 7
  PathIsUNCW                                   fan-in 6
  PathRemoveFileSpecW                          fan-in 6
  PathCreateFromUrlW                           fan-in 5
  PathFileExistsW                              fan-in 5
  PathParseIconLocationW                       fan-in 5
  PathStripToRootW                             fan-in 5
  StrCmpNW                                     fan-in 5
  StrCmpW                                      fan-in 5
  StrDupW                                      fan-in 5
  PathGetDriveNumberW                          fan-in 4
  StrRetToBufW                                 fan-in 4
  StrToIntW                                    fan-in 4
  AssocQueryStringW                            fan-in 3
  PathAppendW                                  fan-in 3
  PathCompactPathExW                           fan-in 3
  PathIsRelativeW                              fan-in 3
  PathIsURLW                                   fan-in 3
  SHCreateStreamOnFileEx                       fan-in 3
  StrCmpIW                                     fan-in 3
  UrlCanonicalizeW                             fan-in 3
  UrlEscapeW                                   fan-in 3
  UrlGetPartW                                  fan-in 3
  UrlIsW                                       fan-in 3
  IntlStrEqWorkerW                             fan-in 2
  PathCombineW                                 fan-in 2
  PathIsRootW                                  fan-in 2
  PathIsUNCServerShareW                        fan-in 2
  PathSkipRootW                                fan-in 2
  StrCmpNIW                                    fan-in 2
  StrFormatByteSizeW                           fan-in 2
  StrToIntA                                    fan-in 2
  StrToIntExW                                  fan-in 2
  ChrCmpIW                                     fan-in 1
  PathBuildRootW                               fan-in 1
  PathCompactPathW                             fan-in 1
  PathCreateFromUrlA                           fan-in 1
  PathGetArgsW                                 fan-in 1
  PathIsContentTypeW                           fan-in 1
  PathIsDirectoryW                             fan-in 1
  PathIsNetworkPathW                           fan-in 1
  PathIsUNCServerW                             fan-in 1
  PathMatchSpecExW                             fan-in 1
  PathMatchSpecW                               fan-in 1
  PathRemoveFileSpecA                          fan-in 1
  PathUnquoteSpacesW                           fan-in 1
  SHCopyKeyW                                   fan-in 1
  SHOpenRegStream2W                            fan-in 1
  StrRetToStrW                                 fan-in 1
  StrStrA                                      fan-in 1
  StrStrNIW                                    fan-in 1
  UrlApplySchemeW                              fan-in 1
  UrlCombineW                                  fan-in 1
  UrlCompareW                                  fan-in 1
  UrlCreateFromPathW                           fan-in 1
  UrlHashW                                     fan-in 1
```

### api-ms-win-core-winrt-error-l1-1-0.dll — 2 candidates

```
  SetRestrictedErrorInfo                       fan-in 77
  GetRestrictedErrorInfo                       fan-in 58
```

### api-ms-win-core-com-l1-1-0.dll — 6 candidates

```
  CreateStreamOnHGlobal                        fan-in 45
  CLSIDFromString                              fan-in 33
  StringFromCLSID                              fan-in 29
  PropVariantCopy                              fan-in 11
  GetHGlobalFromStream                         fan-in 6
  StringFromIID                                fan-in 5
```

### api-ms-win-eventing-classicprovider-l1-1-0.dll — 2 candidates

```
  UnregisterTraceGuids                         fan-in 61
  RegisterTraceGuidsW                          fan-in 58
```

### kernelbase.dll — 67 candidates

```
  FindPackagesByPackageFamily                  fan-in 5
  GetStagedPackagePathByFullName2              fan-in 5
  BaseFormatObjectAttributes                   fan-in 4
  GetPackageFamilyNameFromToken                fan-in 4
  GetStagedPackagePathByFullName               fan-in 4
  ParseApplicationUserModelId                  fan-in 4
  lstrcmpiW                                    fan-in 4
  lstrlenW                                     fan-in 4
  AppContainerRegisterSid                      fan-in 3
  AppContainerUnregisterSid                    fan-in 3
  GetApplicationUserModelIdFromToken           fan-in 3
  GetSystemMetadataPathForPackage              fan-in 3
  lstrcmpW                                     fan-in 3
  AppContainerDeriveSidFromMoniker             fan-in 2
  CompareStringA                               fan-in 2
  GetPackagePathByFullName                     fan-in 2
  GetPackagePathByFullName2                    fan-in 2
  LoadStringBaseExW                            fan-in 2
  NotifyRedirectedStringChange                 fan-in 2
  SHLoadIndirectStringInternal                 fan-in 2
  lstrcmpiA                                    fan-in 2
  AppXGetPackageSid                            fan-in 1
  ChrCmpIA                                     fan-in 1
  ChrCmpIW                                     fan-in 1
  FindPackageDependency                        fan-in 1
  FormatApplicationUserModelId                 fan-in 1
  FormatApplicationUserModelIdA                fan-in 1
  GetCurrentPackagePath                        fan-in 1
  GetCurrentPackagePath2                       fan-in 1
  GetEffectivePackageStatusForUserSid          fan-in 1
  GetEightBitStringToUnicodeStringRoutine      fan-in 1
  GetEraNameCountedString                      fan-in 1
  GetHivePath                                  fan-in 1
  GetNumberFormatW                             fan-in 1
  GetPackageApplicationPropertyString          fan-in 1
  GetPackageFamilyNameFromFilePath             fan-in 1
  GetPackageFullNameFromToken                  fan-in 1
  GetPackageGraphRevisionId                    fan-in 1
  GetPackagePath                               fan-in 1
  GetPackagePathOnVolume                       fan-in 1
  GetPackagePropertyString                     fan-in 1
  GetPackageStatusForUserSid                   fan-in 1
  GetPackageVolumeSisPath                      fan-in 1
  GetStringTableEntry                          fan-in 1
  GetStringTypeA                               fan-in 1
  GetSystemMetadataPath                        fan-in 1
  GetSystemMetadataPathForPackageFamily        fan-in 1
  GetUnicodeStringToEightBitSizeRoutine        fan-in 1
  GetUnicodeStringToEightBitStringRoutine      fan-in 1
  GetUserOverrideString                        fan-in 1
  Internal_EnumDateFormats                     fan-in 1
  Internal_EnumTimeFormats                     fan-in 1
  IsOnDemandRegistrationSupportedForExtensionCategory fan-in 1
  IsSideloadingEnabled                         fan-in 1
  IsSideloadingPolicyApplied                   fan-in 1
  OpenStateExplicitForUserSid                  fan-in 1
  OpenStateExplicitForUserSidString            fan-in 1
  PackageSidFromFamilyName                     fan-in 1
  PackageSidFromProductId                      fan-in 1
  ParseApplicationUserModelIdA                 fan-in 1
  ... and 7 more
```

### api-ms-win-core-url-l1-1-0.dll — 30 candidates

```
  PathIsURLW                                   fan-in 16
  UrlEscapeW                                   fan-in 11
  PathCreateFromUrlW                           fan-in 8
  UrlCreateFromPathW                           fan-in 8
  ParseURLW                                    fan-in 7
  UrlCanonicalizeW                             fan-in 7
  UrlGetPartW                                  fan-in 7
  UrlIsW                                       fan-in 6
  UrlCombineW                                  fan-in 5
  UrlCompareW                                  fan-in 4
  UrlGetLocationW                              fan-in 4
  UrlApplySchemeW                              fan-in 3
  ParseURLA                                    fan-in 2
  PathCreateFromUrlA                           fan-in 2
  UrlCanonicalizeA                             fan-in 2
  UrlCombineA                                  fan-in 2
  UrlFixupW                                    fan-in 2
  UrlHashW                                     fan-in 2
  PathIsURLA                                   fan-in 1
  UrlApplySchemeA                              fan-in 1
  UrlCompareA                                  fan-in 1
  UrlCreateFromPathA                           fan-in 1
  UrlEscapeA                                   fan-in 1
  UrlGetLocationA                              fan-in 1
  UrlGetPartA                                  fan-in 1
  UrlIsA                                       fan-in 1
  UrlIsNoHistoryA                              fan-in 1
  UrlIsNoHistoryW                              fan-in 1
  UrlIsOpaqueA                                 fan-in 1
  UrlIsOpaqueW                                 fan-in 1
```

### api-ms-win-core-path-l1-1-0.dll — 8 candidates

```
  PathCchAppend                                fan-in 30
  PathCchCombine                               fan-in 26
  PathCchSkipRoot                              fan-in 16
  PathIsUNCEx                                  fan-in 10
  PathCchCanonicalize                          fan-in 7
  PathCchStripToRoot                           fan-in 6
  PathCchStripPrefix                           fan-in 2
  PathCchIsRoot                                fan-in 1
```

### api-ms-win-core-string-l2-1-0.dll — 8 candidates

```
  CharNextW                                    fan-in 33
  CharLowerW                                   fan-in 20
  CharPrevW                                    fan-in 10
  CharUpperW                                   fan-in 10
  IsCharAlphaW                                 fan-in 6
  IsCharAlphaNumericW                          fan-in 3
  IsCharLowerW                                 fan-in 2
  IsCharUpperW                                 fan-in 1
```

### msvcp_win.dll — 4 candidates

```
  _Mtx_destroy_in_situ                         fan-in 44
  _Cnd_destroy_in_situ                         fan-in 25
  _Strcoll                                     fan-in 4
  _Strxfrm                                     fan-in 4
```

### advapi32.dll — 31 candidates

```
  GetTokenInformation                          fan-in 7
  ConvertStringSecurityDescriptorToSecurityDescriptorW fan-in 5
  EqualSid                                     fan-in 4
  RegisterTraceGuidsW                          fan-in 4
  UnregisterTraceGuids                         fan-in 4
  CopySid                                      fan-in 3
  GetLengthSid                                 fan-in 3
  AdjustTokenPrivileges                        fan-in 2
  CryptCreateHash                              fan-in 2
  CryptDestroyHash                             fan-in 2
  CryptGetHashParam                            fan-in 2
  CryptHashData                                fan-in 2
  DuplicateToken                               fan-in 2
  DuplicateTokenEx                             fan-in 2
  GetSidSubAuthority                           fan-in 2
  GetSidSubAuthorityCount                      fan-in 2
  IsValidSid                                   fan-in 2
  LookupAccountSidW                            fan-in 2
  SetThreadToken                               fan-in 2
  SetTokenInformation                          fan-in 2
  BuildTrusteeWithSidW                         fan-in 1
  CheckTokenMembership                         fan-in 1
  CreateWellKnownSid                           fan-in 1
  CryptDestroyKey                              fan-in 1
  CryptSetHashParam                            fan-in 1
  CryptSignHashW                               fan-in 1
  GetSecurityDescriptorLength                  fan-in 1
  GetSidIdentifierAuthority                    fan-in 1
  GetSidLengthRequired                         fan-in 1
  RevertToSelf                                 fan-in 1
  SaferComputeTokenFromLevel                   fan-in 1
```

### api-ms-win-core-winrt-error-l1-1-1.dll — 1 candidates

```
  RoGetMatchingRestrictedErrorInfo             fan-in 60
```

### api-ms-win-core-processthreads-l1-1-0.dll — 1 candidates

```
  SetThreadToken                               fan-in 57
```

### api-ms-win-core-datetime-l1-1-0.dll — 4 candidates

```
  GetTimeFormatW                               fan-in 16
  GetDateFormatW                               fan-in 15
  GetTimeFormatA                               fan-in 8
  GetDateFormatA                               fan-in 7
```

### rpcrt4.dll — 9 candidates

```
  UuidCreate                                   fan-in 26
  UuidToStringW                                fan-in 11
  UuidHash                                     fan-in 2
  MesDecodeBufferHandleCreate                  fan-in 1
  MesDecodeIncrementalHandleCreate             fan-in 1
  MesEncodeFixedBufferHandleCreate             fan-in 1
  MesEncodeIncrementalHandleCreate             fan-in 1
  UuidEqual                                    fan-in 1
  UuidToStringA                                fan-in 1
```

### shcore.dll — 16 candidates

```
  IStream_Read                                 fan-in 6
  IStream_Size                                 fan-in 6
  SHCreateMemStream                            fan-in 5
  SHStrDupW                                    fan-in 5
  CreateRandomAccessStreamOverStream           fan-in 4
  IStream_Reset                                fan-in 4
  IStream_Write                                fan-in 3
  IStream_ReadStr                              fan-in 2
  IStream_WriteStr                             fan-in 2
  SHStrDupA                                    fan-in 2
  CreateRandomAccessStreamOnFile               fan-in 1
  CreateStreamOverRandomAccessStream           fan-in 1
  IStream_Copy                                 fan-in 1
  SHCreateStreamOnFileEx                       fan-in 1
  SHCreateStreamOnFileW                        fan-in 1
  SHOpenRegStream2W                            fan-in 1
```

### bcrypt.dll — 7 candidates

```
  BCryptCreateHash                             fan-in 9
  BCryptDestroyHash                            fan-in 9
  BCryptFinishHash                             fan-in 9
  BCryptHashData                               fan-in 9
  BCryptDestroyKey                             fan-in 4
  BCryptDuplicateHash                          fan-in 1
  BCryptHash                                   fan-in 1
```

### api-ms-win-core-stringansi-l1-1-0.dll — 12 candidates

```
  CharNextA                                    fan-in 11
  CharLowerA                                   fan-in 5
  CharPrevA                                    fan-in 4
  CharUpperA                                   fan-in 4
  CharUpperBuffA                               fan-in 3
  IsCharAlphaNumericA                          fan-in 3
  CharNextExA                                  fan-in 2
  IsCharAlphaA                                 fan-in 2
  CharLowerBuffA                               fan-in 1
  CharPrevExA                                  fan-in 1
  IsCharLowerA                                 fan-in 1
  IsCharUpperA                                 fan-in 1
```

### api-ms-win-core-libraryloader-l1-2-1.dll — 1 candidates

```
  FindResourceW                                fan-in 37
```

### api-ms-win-crt-convert-l1-1-0.dll — 9 candidates

```
  strtod                                       fan-in 10
  wcstod                                       fan-in 9
  strtof                                       fan-in 4
  strtoull                                     fan-in 4
  wcstof                                       fan-in 3
  strtoll                                      fan-in 2
  wcstoull                                     fan-in 2
  mbstowcs_s                                   fan-in 1
  wcstoll                                      fan-in 1
```

### api-ms-win-core-string-l2-1-1.dll — 1 candidates

```
  SHLoadIndirectString                         fan-in 30
```

### api-ms-win-core-file-l1-2-0.dll — 2 candidates

```
  GetTempPathW                                 fan-in 24
  GetVolumePathNamesForVolumeNameW             fan-in 5
```

### shell32.dll — 9 candidates

```
  SHGetKnownFolderPath                         fan-in 8
  SHParseDisplayName                           fan-in 8
  SHGetPathFromIDListW                         fan-in 5
  SHCreateAssociationRegistration              fan-in 1
  SHGetFolderPathEx                            fan-in 1
  SHGetFolderPathW                             fan-in 1
  SHGetPathFromIDListA                         fan-in 1
  SHGetSpecialFolderPathW                      fan-in 1
  SHPathPrepareForWriteW                       fan-in 1
```

### api-ms-win-shcore-stream-l1-1-0.dll — 8 candidates

```
  SHCreateMemStream                            fan-in 6
  IStream_Write                                fan-in 4
  IStream_Read                                 fan-in 3
  IStream_Reset                                fan-in 3
  IStream_Size                                 fan-in 3
  SHCreateStreamOnFileEx                       fan-in 3
  SHCreateStreamOnFileW                        fan-in 3
  SHOpenRegStream2W                            fan-in 1
```

### api-ms-win-core-localization-obsolete-l1-2-0.dll — 3 candidates

```
  CompareStringA                               fan-in 17
  GetNumberFormatW                             fan-in 7
  GetStringTypeA                               fan-in 1
```

### api-ms-win-core-privateprofile-l1-1-0.dll — 4 candidates

```
  GetPrivateProfileStringW                     fan-in 10
  WritePrivateProfileStringW                   fan-in 9
  GetProfileStringW                            fan-in 5
  GetPrivateProfileStringA                     fan-in 1
```

### api-ms-win-core-crt-l1-1-0.dll — 13 candidates

```
  memset                                       fan-in 4
  memcpy                                       fan-in 3
  memmove                                      fan-in 3
  memcmp                                       fan-in 2
  memcpy_s                                     fan-in 2
  memmove_s                                    fan-in 2
  wcsstr                                       fan-in 2
  strchr                                       fan-in 1
  strncmp                                      fan-in 1
  strnlen                                      fan-in 1
  strrchr                                      fan-in 1
  strstr                                       fan-in 1
  wcsnlen                                      fan-in 1
```

### crypt32.dll — 17 candidates

```
  CertFindCertificateInStore                   fan-in 4
  CryptDecodeObjectEx                          fan-in 3
  CertFindExtension                            fan-in 2
  CertAddEncodedCertificateToStore             fan-in 1
  CertCompareCertificateName                   fan-in 1
  CertFindCRLInStore                           fan-in 1
  CertFindCTLInStore                           fan-in 1
  CertFindCertificateInCRL                     fan-in 1
  CertFindSubjectInCTL                         fan-in 1
  CertGetNameStringA                           fan-in 1
  CertNameToStrA                               fan-in 1
  CertNameToStrW                               fan-in 1
  CryptDecodeObject                            fan-in 1
  CryptEncodeObjectEx                          fan-in 1
  CryptHashCertificate                         fan-in 1
  CryptSignAndEncodeCertificate                fan-in 1
  I_CryptFindLruEntry                          fan-in 1
```

### api-ms-win-core-file-l1-2-3.dll — 2 candidates

```
  GetTempPath2W                                fan-in 21
  GetTempPath2A                                fan-in 1
```

### api-ms-win-appmodel-runtime-l1-1-1.dll — 6 candidates

```
  FindPackagesByPackageFamily                  fan-in 6
  GetApplicationUserModelIdFromToken           fan-in 5
  ParseApplicationUserModelId                  fan-in 4
  GetPackageFullNameFromToken                  fan-in 3
  GetPackagePathByFullName                     fan-in 2
  GetPackageFamilyNameFromToken                fan-in 1
```

### windows.storage.dll — 12 candidates

```
  SHGetKnownFolderPath                         fan-in 4
  ILFindLastID                                 fan-in 3
  ILIsEqual                                    fan-in 3
  SHParseDisplayName                           fan-in 3
  CStorageItem_GetValidatedStorageItemObject   fan-in 1
  ILFindChild                                  fan-in 1
  PathSeperateArgs                             fan-in 1
  SHGetFolderPathEx                            fan-in 1
  SHGetPathFromIDListEx                        fan-in 1
  STORAGE_CStorageItem_GetValidatedStorageItem fan-in 1
  STORAGE_CStorageItem_GetValidatedStorageItemObject fan-in 1
  STORAGE_PathIsEqualOrSubFolderOfKnownFolders fan-in 1
```

### api-ms-win-core-file-l2-1-2.dll — 1 candidates

```
  CopyFileW                                    fan-in 19
```

### api-ms-win-security-sddl-l1-1-0.dll — 2 candidates

```
  ConvertStringSecurityDescriptorToSecurityDescriptorW fan-in 18
  ConvertSecurityDescriptorToStringSecurityDescriptorW fan-in 1
```

### propsys.dll — 12 candidates

```
  PSFormatForDisplay                           fan-in 3
  PSPropertyBag_WriteStr                       fan-in 3
  PropVariantCompareEx                         fan-in 3
  PropVariantToString                          fan-in 2
  InitPropVariantFromStringAsVector            fan-in 1
  InitPropVariantFromStringVector              fan-in 1
  PSGetPropertyDescriptionListFromString       fan-in 1
  PSPropertyBag_ReadStr                        fan-in 1
  PSPropertyBag_ReadStream                     fan-in 1
  PSPropertyBag_WriteStream                    fan-in 1
  PropVariantToStrRet                          fan-in 1
  PropVariantToStringWithDefault               fan-in 1
```

### api-ms-win-shcore-stream-winrt-l1-1-0.dll — 3 candidates

```
  CreateRandomAccessStreamOverStream           fan-in 12
  CreateStreamOverRandomAccessStream           fan-in 3
  CreateRandomAccessStreamOnFile               fan-in 1
```

### api-ms-win-downlevel-kernel32-l1-1-0.dll — 10 candidates

```
  DecodePointer                                fan-in 2
  EncodePointer                                fan-in 2
  FormatMessageW                               fan-in 2
  MultiByteToWideChar                          fan-in 2
  OutputDebugStringW                           fan-in 2
  WideCharToMultiByte                          fan-in 2
  CompareFileTime                              fan-in 1
  CompareStringEx                              fan-in 1
  ExpandEnvironmentStringsW                    fan-in 1
  LCMapStringEx                                fan-in 1
```

### api-ms-win-core-file-l2-1-0.dll — 2 candidates

```
  CopyFileExW                                  fan-in 9
  CopyFile2                                    fan-in 5
```

### api-ms-win-core-normalization-l1-1-0.dll — 3 candidates

```
  GetStringScripts                             fan-in 9
  NormalizeString                              fan-in 4
  IsNormalizedString                           fan-in 1
```

### api-ms-win-security-base-l1-2-0.dll — 2 candidates

```
  CheckTokenCapability                         fan-in 7
  CheckTokenMembershipEx                       fan-in 7
```

### api-ms-win-core-localization-l2-1-0.dll — 8 candidates

```
  EnumTimeFormatsEx                            fan-in 3
  GetNumberFormatEx                            fan-in 3
  EnumDateFormatsExEx                          fan-in 2
  GetCurrencyFormatEx                          fan-in 2
  EnumDateFormatsExW                           fan-in 1
  EnumDateFormatsW                             fan-in 1
  EnumTimeFormatsW                             fan-in 1
  GetCurrencyFormatW                           fan-in 1
```

### api-ms-win-core-datetime-l1-1-1.dll — 2 candidates

```
  GetDateFormatEx                              fan-in 7
  GetTimeFormatEx                              fan-in 6
```

### api-ms-win-core-atoms-l1-1-0.dll — 4 candidates

```
  FindAtomW                                    fan-in 5
  GlobalFindAtomW                              fan-in 4
  FindAtomA                                    fan-in 2
  GlobalFindAtomA                              fan-in 2
```

### api-ms-win-core-kernel32-legacy-l1-1-0.dll — 6 candidates

```
  FindResourceExA                              fan-in 5
  FindResourceA                                fan-in 2
  GetShortPathNameA                            fan-in 2
  GetStringTypeExA                             fan-in 2
  CopyFileA                                    fan-in 1
  CopyFileW                                    fan-in 1
```

### api-ms-win-core-wow64-l1-1-0.dll — 1 candidates

```
  Wow64RevertWow64FsRedirection                fan-in 12
```

### vcruntime140_clr0400.dll — 7 candidates

```
  memmove                                      fan-in 3
  memcmp                                       fan-in 2
  memcpy                                       fan-in 2
  memset                                       fan-in 2
  strchr                                       fan-in 1
  strrchr                                      fan-in 1
  wcsstr                                       fan-in 1
```

### api-ms-win-core-psm-key-l1-1-0.dll — 1 candidates

```
  PsmGetKeyFromToken                           fan-in 10
```

### api-ms-win-rtcore-ntuser-window-l1-1-0.dll — 2 candidates

```
  DestroyWindow                                fan-in 7
  FindWindowW                                  fan-in 3
```

### api-ms-win-core-sidebyside-l1-1-0.dll — 2 candidates

```
  FindActCtxSectionStringW                     fan-in 6
  FindActCtxSectionGuid                        fan-in 3
```

### api-ms-win-shlwapi-winrt-storage-l1-1-1.dll — 5 candidates

```
  AssocQueryStringW                            fan-in 2
  StrRetToBufW                                 fan-in 2
  StrRetToStrW                                 fan-in 2
  IStream_ReadPidl                             fan-in 1
  PathCompactPathExW                           fan-in 1
```

### msvcp140_app.dll — 4 candidates

```
  _Cnd_destroy_in_situ                         fan-in 2
  _Mtx_destroy_in_situ                         fan-in 2
  _Strcoll                                     fan-in 2
  _Strxfrm                                     fan-in 2
```

### api-ms-win-mm-mme-l1-1-0.dll — 8 candidates

```
  midiStreamClose                              fan-in 1
  midiStreamOpen                               fan-in 1
  midiStreamOut                                fan-in 1
  midiStreamPause                              fan-in 1
  midiStreamPosition                           fan-in 1
  midiStreamProperty                           fan-in 1
  midiStreamRestart                            fan-in 1
  midiStreamStop                               fan-in 1
```

### api-ms-win-core-file-l1-2-2.dll — 2 candidates

```
  GetTempPathA                                 fan-in 6
  FindFirstStreamW                             fan-in 1
```

### api-ms-win-core-localization-private-l1-1-0.dll — 3 candidates

```
  LoadStringByReference                        fan-in 5
  _AddMUIStringToCache                         fan-in 1
  _OpenMuiStringCache                          fan-in 1
```

### api-ms-win-core-calendar-l1-1-0.dll — 3 candidates

```
  ConvertCalDateTimeToSystemTime               fan-in 3
  ConvertSystemTimeToCalDateTime               fan-in 3
  GetCalendarDateFormatEx                      fan-in 1
```

### iphlpapi.dll — 4 candidates

```
  ConvertInterfaceGuidToLuid                   fan-in 2
  ConvertInterfaceLuidToIndex                  fan-in 2
  ConvertInterfaceLuidToNameW                  fan-in 2
  ConvertInterfaceLuidToAlias                  fan-in 1
```

### api-ms-win-core-console-l2-1-0.dll — 7 candidates

```
  FillConsoleOutputAttribute                   fan-in 1
  FillConsoleOutputCharacterA                  fan-in 1
  FillConsoleOutputCharacterW                  fan-in 1
  ReadConsoleOutputCharacterA                  fan-in 1
  ReadConsoleOutputCharacterW                  fan-in 1
  WriteConsoleOutputCharacterA                 fan-in 1
  WriteConsoleOutputCharacterW                 fan-in 1
```

### iertutil.dll — 7 candidates

```
  CreateStringHashN                            fan-in 1
  CreateUriFromMultiByteString                 fan-in 1
  GetPortFromUrlScheme                         fan-in 1
  IntlPercentEncodeNormalize                   fan-in 1
  IsStringProperty                             fan-in 1
  PrivateCoInternetCanonicalizeIUri            fan-in 1
  PrivateCoInternetParseIUri                   fan-in 1
```

### api-ms-win-core-processthreads-l1-1-1.dll — 1 candidates

```
  FlushInstructionCache                        fan-in 6
```

### api-ms-win-core-processenvironment-l1-2-0.dll — 3 candidates

```
  SearchPathA                                  fan-in 4
  NeedCurrentDirectoryForExePathA              fan-in 1
  NeedCurrentDirectoryForExePathW              fan-in 1
```

### api-ms-win-ntuser-rectangle-l1-1-0.dll — 2 candidates

```
  CopyRect                                     fan-in 4
  EqualRect                                    fan-in 2
```

### api-ms-win-crt-time-l1-1-0.dll — 3 candidates

```
  _Strftime                                    fan-in 3
  strftime                                     fan-in 2
  wcsftime                                     fan-in 1
```

### api-ms-win-shell-namespace-l1-1-0.dll — 3 candidates

```
  ILFindLastID                                 fan-in 2
  ILIsEqual                                    fan-in 2
  SHParseDisplayName                           fan-in 2
```

### coremessaging.dll — 4 candidates

```
  CoreUICallCreateConversationHost             fan-in 2
  MsgStringCreateShared                        fan-in 2
  CoreUICreateAnonymousStream                  fan-in 1
  MsgStringCreateStack                         fan-in 1
```

### searchindexercore.dll — 4 candidates

```
  sqlite3VarintLen                             fan-in 2
  sqlite3_errstr                               fan-in 2
  sqlite3_memory_highwater                     fan-in 1
  sqlite3_memory_used                          fan-in 1
```

### ucrtbase_clr0400.dll — 5 candidates

```
  strncmp                                      fan-in 2
  _strdup                                      fan-in 1
  strnlen                                      fan-in 1
  wcsnlen                                      fan-in 1
  wmemcpy_s                                    fan-in 1
```

### api-ms-win-core-console-l3-2-0.dll — 6 candidates

```
  GetConsoleAliasExesLengthA                   fan-in 1
  GetConsoleAliasExesLengthW                   fan-in 1
  GetConsoleAliasesLengthA                     fan-in 1
  GetConsoleAliasesLengthW                     fan-in 1
  GetConsoleCommandHistoryLengthA              fan-in 1
  GetConsoleCommandHistoryLengthW              fan-in 1
```

### setupapi.dll — 1 candidates

```
  SetupDiDestroyDeviceInfoList                 fan-in 5
```

### gdiplus.dll — 5 candidates

```
  GdipBitmapLockBits                           fan-in 1
  GdipBitmapUnlockBits                         fan-in 1
  GdipCreateBitmapFromScan0                    fan-in 1
  GdipCreateBitmapFromStream                   fan-in 1
  GdipGetImagePixelFormat                      fan-in 1
```

### webservices.dll — 5 candidates

```
  WsCopyNode                                   fan-in 1
  WsEndReaderCanonicalization                  fan-in 1
  WsFillReader                                 fan-in 1
  WsGetErrorString                             fan-in 1
  WsStartReaderCanonicalization                fan-in 1
```

### ws2_32.dll — 5 candidates

```
  WPUGetProviderPathEx                         fan-in 1
  WSAAddressToStringW                          fan-in 1
  WSAStringToAddressW                          fan-in 1
  WSCGetProviderPath                           fan-in 1
  WahDestroyHandleContextTable                 fan-in 1
```

### api-ms-win-crt-stdio-l1-1-0.dll — 1 candidates

```
  _get_stream_buffer_pointers                  fan-in 4
```

### api-ms-win-devices-query-l1-1-0.dll — 1 candidates

```
  DevFindProperty                              fan-in 4
```

### api-ms-win-rtcore-ntuser-clipboard-l1-1-0.dll — 1 candidates

```
  RegisterClipboardFormatW                     fan-in 4
```

### api-ms-win-security-appcontainer-l1-1-0.dll — 1 candidates

```
  GetAppContainerNamedObjectPath               fan-in 4
```

### wininet.dll — 1 candidates

```
  InternetCrackUrlW                            fan-in 4
```

### api-ms-win-shcore-obsolete-l1-1-0.dll — 2 candidates

```
  SHStrDupW                                    fan-in 3
  SHStrDupA                                    fan-in 1
```

### api-ms-win-security-audit-l1-1-1.dll — 4 candidates

```
  AuditEnumerateCategories                     fan-in 1
  AuditEnumerateSubCategories                  fan-in 1
  AuditLookupCategoryNameW                     fan-in 1
  AuditLookupSubCategoryNameW                  fan-in 1
```

### api-ms-win-core-winrt-l1-1-0.dll — 1 candidates

```
  RoRevokeActivationFactories                  fan-in 3
```

### windowscodecs.dll — 1 candidates

```
  WICConvertBitmapSource                       fan-in 3
```

### api-ms-win-core-fibers-l2-1-0.dll — 2 candidates

```
  ConvertThreadToFiber                         fan-in 2
  ConvertFiberToThread                         fan-in 1
```

### api-ms-win-core-misc-l1-1-0.dll — 3 candidates

```
  FormatMessageW                               fan-in 1
  lstrcmpiA                                    fan-in 1
  lstrlenW                                     fan-in 1
```

### ext-ms-win-gdi-draw-l1-1-0.dll — 3 candidates

```
  CreateBitmap                                 fan-in 1
  GetDIBits                                    fan-in 1
  StretchDIBits                                fan-in 1
```

### api-ms-win-appmodel-runtime-l1-1-3.dll — 1 candidates

```
  GetStagedPackagePathByFullName2              fan-in 2
```

### api-ms-win-core-datetime-l1-1-2.dll — 1 candidates

```
  GetDurationFormatEx                          fan-in 2
```

### api-ms-win-core-libraryloader-l1-1-0.dll — 1 candidates

```
  FindResourceExW                              fan-in 2
```

### api-ms-win-eventing-obsolete-l1-1-0.dll — 1 candidates

```
  RegisterTraceGuidsA                          fan-in 2
```

### api-ms-win-security-lsalookup-l2-1-0.dll — 1 candidates

```
  LookupAccountSidW                            fan-in 2
```

### api-ms-win-security-trustee-l1-1-0.dll — 1 candidates

```
  BuildTrusteeWithSidW                         fan-in 2
```

### api-ms-win-storage-exports-internal-l1-1-0.dll — 1 candidates

```
  SHGetFolderPathEx                            fan-in 2
```

### ext-ms-win-shell32-shellfolders-l1-1-0.dll — 1 candidates

```
  SHGetKnownFolderPath                         fan-in 2
```

### ncrypt.dll — 1 candidates

```
  NCryptSignHash                               fan-in 2
```

### netapi32.dll — 1 candidates

```
  NetGetAadJoinInformation                     fan-in 2
```

### winhttp.dll — 1 candidates

```
  WinHttpCrackUrl                              fan-in 2
```

### wkscli.dll — 1 candidates

```
  NetGetJoinInformation                        fan-in 2
```

### api-ms-win-core-comm-l1-1-0.dll — 2 candidates

```
  EscapeCommFunction                           fan-in 1
  TransmitCommChar                             fan-in 1
```

### api-ms-win-core-localization-l1-1-0.dll — 2 candidates

```
  LCMapStringEx                                fan-in 1
  LCMapStringW                                 fan-in 1
```

### api-ms-win-gdi-internal-uap-l1-1-0.dll — 2 candidates

```
  FillRgn                                      fan-in 1
  SetPolyFillModeImpl                          fan-in 1
```

### api-ms-win-service-private-l1-1-0.dll — 2 candidates

```
  I_ScSetServiceBitsA                          fan-in 1
  I_ScSetServiceBitsW                          fan-in 1
```

### api-ms-win-winrt-search-folder-l1-1-0.dll — 2 candidates

```
  IsMSSearchEnabled                            fan-in 1
  SHCreateSearchIDListFromAutoList             fan-in 1
```

### avrt.dll — 2 candidates

```
  AvRevertMmThreadCharacteristics              fan-in 1
  AvSetMmThreadCharacteristicsW                fan-in 1
```

### bootvid.dll — 2 candidates

```
  VidDisplayString                             fan-in 1
  VidSolidColorFill                            fan-in 1
```

### ci.dll — 2 candidates

```
  MesDecodeBufferHandleCreate                  fan-in 1
  MesEncodeIncrementalHandleCreate             fan-in 1
```

### icu.dll — 2 candidates

```
  ucol_getSortKey                              fan-in 1
  ucol_setStrength                             fan-in 1
```

### icuuc.dll — 2 candidates

```
  u_strToLower                                 fan-in 1
  u_strToUpper                                 fan-in 1
```

### imm32.dll — 2 candidates

```
  ImmGetCompositionStringW                     fan-in 1
  ImmSetCompositionStringW                     fan-in 1
```

### ole32.dll — 2 candidates

```
  CLSIDFromString                              fan-in 1
  CreateStreamOnHGlobal                        fan-in 1
```

### urlmon.dll — 2 candidates

```
  UrlMkGetSessionOption                        fan-in 1
  UrlMkSetSessionOption                        fan-in 1
```

### api-ms-win-appmodel-identity-l1-2-0.dll — 1 candidates

```
  AppXGetPackageSid                            fan-in 1
```

### api-ms-win-appmodel-runtime-internal-l1-1-4.dll — 1 candidates

```
  GetEffectivePackageStatusForUserSid          fan-in 1
```

### api-ms-win-core-appcompat-l1-1-0.dll — 1 candidates

```
  BaseIsAppcompatInfrastructureDisabled        fan-in 1
```

### api-ms-win-core-featuretoggles-l1-1-0.dll — 1 candidates

```
  GetFeatureTogglesChangeToken                 fan-in 1
```

### api-ms-win-core-fibers-l2-1-1.dll — 1 candidates

```
  ConvertThreadToFiberEx                       fan-in 1
```

### api-ms-win-core-handle-l1-1-0.dll — 1 candidates

```
  CompareObjectHandles                         fan-in 1
```

### api-ms-win-core-kernel32-private-l1-1-0.dll — 1 candidates

```
  CompareCalendarDates                         fan-in 1
```

### api-ms-win-core-kernel32-private-l1-1-1.dll — 1 candidates

```
  PrivCopyFileExW                              fan-in 1
```

### api-ms-win-core-localization-ansi-l1-1-0.dll — 1 candidates

```
  GetStringTypeExA                             fan-in 1
```

### api-ms-win-core-version-l1-1-0.dll — 1 candidates

```
  VerFindFileW                                 fan-in 1
```

### api-ms-win-core-versionansi-l1-1-0.dll — 1 candidates

```
  VerFindFileA                                 fan-in 1
```

### api-ms-win-core-winrt-registration-l1-1-0.dll — 1 candidates

```
  RoGetActivatableClassRegistration            fan-in 1
```

### api-ms-win-core-xstate-l2-1-0.dll — 1 candidates

```
  CopyContext                                  fan-in 1
```

### api-ms-win-eventing-controller-l1-1-0.dll — 1 candidates

```
  EnumerateTraceGuidsEx                        fan-in 1
```

### api-ms-win-rtcore-ntuser-private-l1-1-0.dll — 1 candidates

```
  DestroyDCompositionHwndTarget                fan-in 1
```

### api-ms-win-rtcore-ntuser-private-l1-1-11.dll — 1 candidates

```
  GetDCompositionHwndBitmap                    fan-in 1
```

### api-ms-win-security-audit-l1-1-0.dll — 1 candidates

```
  AuditComputeEffectivePolicyBySid             fan-in 1
```

### api-ms-win-security-base-l1-2-2.dll — 1 candidates

```
  DeriveCapabilitySidsFromName                 fan-in 1
```

### api-ms-win-security-base-private-l1-1-1.dll — 1 candidates

```
  CreateAppContainerToken                      fan-in 1
```

### api-ms-win-security-lsalookup-l1-1-0.dll — 1 candidates

```
  LookupAccountSidLocalW                       fan-in 1
```

### api-ms-win-security-lsapolicy-l1-1-0.dll — 1 candidates

```
  LsaLookupSids                                fan-in 1
```

### bcp47langs.dll — 1 candidates

```
  Bcp47FindClosestLanguage                     fan-in 1
```

### devobj.dll — 1 candidates

```
  DevObjDestroyDeviceInfoList                  fan-in 1
```

### dnsapi.dll — 1 candidates

```
  DnsValidateName_W                            fan-in 1
```

### dsreg.dll — 1 candidates

```
  DsrIsDeviceJoined                            fan-in 1
```

### ext-ms-win-accel-api-km-l1-1-0.dll — 1 candidates

```
  AccelDestroyOffloadWorkspace                 fan-in 1
```

### ext-ms-win-fs-clfs-l1-1-0.dll — 1 candidates

```
  ClfsLsnEqual                                 fan-in 1
```

### ext-ms-win-ntos-processparameters-l1-1-0.dll — 1 candidates

```
  PsDestroyProcessParameterOverrides           fan-in 1
```

### ext-ms-win-ntos-win32k-l1-1-0.dll — 1 candidates

```
  Win32kJobUpdateUIRestrictionsNotify          fan-in 1
```

### icuin.dll — 1 candidates

```
  ucol_strcoll                                 fan-in 1
```

### imagehlp.dll — 1 candidates

```
  SymGetModuleBase64                           fan-in 1
```

### policymanager.dll — 1 candidates

```
  PolicyManager_GetPolicyString                fan-in 1
```

### rmclient.dll — 1 candidates

```
  HamHostIdFindOrCreate                        fan-in 1
```

### staterepository.core.dll — 1 candidates

```
  sqlite3_result_error_nomem                   fan-in 1
```

### userenv.dll — 1 candidates

```
  DeriveAppContainerSidFromAppContainerName    fan-in 1
```

### win32u.dll — 1 candidates

```
  ABI_Get_tooltipStrings                       fan-in 1
```

