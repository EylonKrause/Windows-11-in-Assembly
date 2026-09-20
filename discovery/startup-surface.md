# The startup path's actual call surface, and the part of it we have

Built by `tools/desktop-surface.py --profile startup` from the **running** system on this machine: the modules currently mapped into the boot and service spine, csrss, wininit, winlogon, services, lsass, every svchost group, LogonUI, fontdrvhost, dwm, dllhost, conhost and the platform services. These processes started at boot and never restarted, so their module set IS what the startup path loaded. **smss.exe is the one gap**: it exits before anything can sample it, so its private imports are not represented here, and the union of their **import tables**.

An export is a function that exists. An import is a function something actually binds to. This is the second thing.

## Totals

| | |
|---|---:|
| startup modules read | **180** |
| distinct (dll, function) bindings | **7432** |
| already converted in `image/tree` | **260** |
| excluded as known-not-a-target | **2796** |
| **remaining candidates** | **4376** |
| of those, string/path/bit shaped | **869** |

The excluded count is not a rounding error and the reasons matter, so they are listed rather than applied silently:

| excluded because | count |
|---|---:|
| syscall stub: the body is a syscall | 1365 |
| C++ mangled, ABI-unstable | 328 |
| ordinal forwarder | 257 |
| allocator/VM: paid in the lock or the kernel | 236 |
| RPC marshalling | 106 |
| synchronisation primitive | 104 |
| registry: paid in the kernel transition | 93 |
| kernel object call: paid in the transition | 73 |
| CRT internal plumbing | 72 |
| loader internals | 53 |
| COM plumbing | 41 |
| NLS/collation: needs the OS tables to be bit-exact | 38 |
| ETW tracing | 29 |
| code-page dependent | 1 |

## Ranked by fan-in — how many of the desktop's own modules bind it

Fan-in is a proxy for pervasiveness, not for time spent. A routine bound by two hundred of this profile's DLLs pays back everywhere at once if it is beatable; it is still only a candidate until it is timed against the live export.

| # | dll | function | startup modules importing it | shaped like a target |
|---:|---|---|---:|:--:|
| 1 | `api-ms-win-core-processthreads-l1-1-0.dll` | `GetCurrentProcessId` | **169** |  |
| 2 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetSystemTimeAsFileTime` | **168** |  |
| 3 | `api-ms-win-core-processthreads-l1-1-0.dll` | `GetCurrentThreadId` | **166** |  |
| 4 | `api-ms-win-core-profile-l1-1-0.dll` | `QueryPerformanceCounter` | **166** |  |
| 5 | `api-ms-win-core-processthreads-l1-1-0.dll` | `GetCurrentProcess` | **165** |  |
| 6 | `api-ms-win-core-processthreads-l1-1-0.dll` | `TerminateProcess` | **160** |  |
| 7 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `GetLastError` | **159** |  |
| 8 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `SetUnhandledExceptionFilter` | **159** |  |
| 9 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `UnhandledExceptionFilter` | **159** |  |
| 10 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `SetLastError` | **144** |  |
| 11 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetProcAddress` | **144** |  |
| 12 | `api-ms-win-core-debug-l1-1-0.dll` | `IsDebuggerPresent` | **142** |  |
| 13 | `api-ms-win-core-delayload-l1-1-0.dll` | `DelayLoadFailureHook` | **140** |  |
| 14 | `api-ms-win-core-delayload-l1-1-1.dll` | `ResolveDelayLoadedAPI` | **137** |  |
| 15 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleHandleW` | **136** |  |
| 16 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetTickCount` | **132** |  |
| 17 | `api-ms-win-core-localization-l1-2-0.dll` | `FormatMessageW` | **130** | yes |
| 18 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleHandleExW` | **126** |  |
| 19 | `api-ms-win-core-string-l1-1-0.dll` | `WideCharToMultiByte` | **126** | yes |
| 20 | `api-ms-win-core-synch-l1-1-0.dll` | `AcquireSRWLockExclusive` | **125** |  |
| 21 | `api-ms-win-core-synch-l1-1-0.dll` | `ReleaseSRWLockExclusive` | **125** |  |
| 22 | `api-ms-win-core-debug-l1-1-0.dll` | `OutputDebugStringW` | **119** | yes |
| 23 | `api-ms-win-core-debug-l1-1-0.dll` | `DebugBreak` | **118** |  |
| 24 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `DisableThreadLibraryCalls` | **118** |  |
| 25 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleFileNameA` | **116** |  |
| 26 | `api-ms-win-core-rtlsupport-l1-1-0.dll` | `RtlLookupFunctionEntry` | **111** |  |
| 27 | `api-ms-win-core-rtlsupport-l1-1-0.dll` | `RtlCaptureContext` | **110** |  |
| 28 | `api-ms-win-core-rtlsupport-l1-1-0.dll` | `RtlVirtualUnwind` | **110** |  |
| 29 | `api-ms-win-core-synch-l1-1-0.dll` | `AcquireSRWLockShared` | **110** |  |
| 30 | `api-ms-win-core-synch-l1-1-0.dll` | `ReleaseSRWLockShared` | **110** |  |
| 31 | `api-ms-win-core-processthreads-l1-1-1.dll` | `IsProcessorFeaturePresent` | **109** |  |
| 32 | `api-ms-win-core-threadpool-l1-2-0.dll` | `SetThreadpoolTimer` | **109** |  |
| 33 | `api-ms-win-crt-runtime-l1-1-0.dll` | `_initterm` | **109** |  |
| 34 | `api-ms-win-crt-runtime-l1-1-0.dll` | `_initterm_e` | **109** |  |
| 35 | `api-ms-win-core-interlocked-l1-1-0.dll` | `InitializeSListHead` | **108** |  |
| 36 | `api-ms-win-crt-string-l1-1-0.dll` | `memset` | **106** | yes |
| 37 | `api-ms-win-crt-private-l1-1-0.dll` | `memcpy` | **102** | yes |
| 38 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `LoadLibraryExW` | **96** |  |
| 39 | `api-ms-win-core-synch-l1-2-0.dll` | `Sleep` | **95** |  |
| 40 | `api-ms-win-core-processthreads-l1-1-0.dll` | `GetCurrentThread` | **94** |  |
| 41 | `api-ms-win-core-synch-l1-1-0.dll` | `CreateEventW` | **94** |  |
| 42 | `api-ms-win-core-errorhandling-l1-1-0.dll` | `RaiseException` | **93** |  |
| 43 | `api-ms-win-crt-private-l1-1-0.dll` | `memcmp` | **90** | yes |
| 44 | `api-ms-win-core-string-l1-1-0.dll` | `MultiByteToWideChar` | **89** | yes |
| 45 | `api-ms-win-crt-private-l1-1-0.dll` | `memmove` | **86** | yes |
| 46 | `api-ms-win-core-synch-l1-2-0.dll` | `InitOnceExecuteOnce` | **85** |  |
| 47 | `api-ms-win-security-base-l1-1-0.dll` | `GetTokenInformation` | **85** | yes |
| 48 | `api-ms-win-core-synch-l1-2-0.dll` | `InitOnceBeginInitialize` | **77** |  |
| 49 | `api-ms-win-core-synch-l1-2-0.dll` | `InitOnceComplete` | **77** |  |
| 50 | `api-ms-win-core-apiquery-l1-1-0.dll` | `ApiSetQueryApiSetPresence` | **76** |  |
| 51 | `api-ms-win-core-synch-l1-1-0.dll` | `InitializeSRWLock` | **76** |  |
| 52 | `ntdll.dll` | `RtlNtStatusToDosError` | **74** |  |
| 53 | `api-ms-win-crt-private-l1-1-0.dll` | `_CxxThrowException` | **73** |  |
| 54 | `api-ms-win-core-util-l1-1-0.dll` | `EncodePointer` | **71** | yes |
| 55 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleFileNameW` | **66** |  |
| 56 | `api-ms-win-core-util-l1-1-0.dll` | `DecodePointer` | **66** | yes |
| 57 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `GetModuleHandleExA` | **59** |  |
| 58 | `ntdll.dll` | `RtlCaptureContext` | **57** |  |
| 59 | `ntdll.dll` | `RtlLookupFunctionEntry` | **57** |  |
| 60 | `ntdll.dll` | `RtlVirtualUnwind` | **57** |  |
| 61 | `api-ms-win-core-synch-l1-1-0.dll` | `CreateEventExW` | **54** |  |
| 62 | `api-ms-win-eventing-provider-l1-1-0.dll` | `EventActivityIdControl` | **54** |  |
| 63 | `api-ms-win-core-processenvironment-l1-1-0.dll` | `ExpandEnvironmentStringsW` | **53** | yes |
| 64 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetTickCount64` | **48** |  |
| 65 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `LoadStringW` | **46** | yes |
| 66 | `api-ms-win-security-base-l1-1-0.dll` | `GetLengthSid` | **46** | yes |
| 67 | `ntdll.dll` | `RtlSubscribeWnfStateChangeNotification` | **45** |  |
| 68 | `api-ms-win-core-string-l1-1-0.dll` | `CompareStringW` | **44** | yes |
| 69 | `api-ms-win-core-file-l1-1-0.dll` | `FindClose` | **43** | yes |
| 70 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetSystemInfo` | **43** |  |
| 71 | `ntdll.dll` | `RtlGetDeviceFamilyInfoEnum` | **42** |  |
| 72 | `api-ms-win-core-file-l1-1-0.dll` | `GetFileAttributesW` | **40** |  |
| 73 | `api-ms-win-core-memory-l1-1-0.dll` | `UnmapViewOfFile` | **40** |  |
| 74 | `api-ms-win-core-threadpool-l1-2-0.dll` | `SubmitThreadpoolWork` | **40** |  |
| 75 | `api-ms-win-core-file-l1-1-0.dll` | `FindFirstFileW` | **39** | yes |
| 76 | `api-ms-win-core-processthreads-l1-1-0.dll` | `TlsSetValue` | **39** |  |
| 77 | `msvcrt.dll` | `_XcptFilter` | **39** |  |
| 78 | `msvcrt.dll` | `_initterm` | **39** |  |
| 79 | `msvcrt.dll` | `free` | **39** |  |
| 80 | `msvcrt.dll` | `malloc` | **39** |  |
| 81 | `api-ms-win-core-threadpool-l1-2-0.dll` | `SetThreadpoolWait` | **38** |  |
| 82 | `api-ms-win-core-file-l1-1-0.dll` | `FindNextFileW` | **37** | yes |
| 83 | `api-ms-win-core-processthreads-l1-1-0.dll` | `TlsGetValue` | **37** |  |
| 84 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetSystemDirectoryW` | **37** |  |
| 85 | `api-ms-win-core-interlocked-l1-1-0.dll` | `InterlockedPushEntrySList` | **36** |  |
| 86 | `api-ms-win-core-libraryloader-l1-2-0.dll` | `LoadResource` | **36** |  |
| 87 | `api-ms-win-core-memory-l1-1-0.dll` | `MapViewOfFile` | **36** |  |
| 88 | `msvcrt.dll` | `memset` | **36** | yes |
| 89 | `msvcrt.dll` | `memcpy` | **35** | yes |
| 90 | `ntdll.dll` | `RtlNtStatusToDosErrorNoTeb` | **35** |  |
| 91 | `ntdll.dll` | `RtlPublishWnfStateData` | **35** |  |
| 92 | `api-ms-win-core-processthreads-l1-1-0.dll` | `SetThreadToken` | **34** | yes |
| 93 | `api-ms-win-core-psapi-l1-1-0.dll` | `QueryFullProcessImageNameW` | **34** |  |
| 94 | `api-ms-win-core-sysinfo-l1-1-0.dll` | `GetSystemTime` | **34** |  |
| 95 | `api-ms-win-core-timezone-l1-1-0.dll` | `SystemTimeToFileTime` | **34** |  |
| 96 | `api-ms-win-security-base-l1-1-0.dll` | `CopySid` | **34** | yes |
| 97 | `api-ms-win-security-base-l1-1-0.dll` | `RevertToSelf` | **34** | yes |
| 98 | `api-ms-win-core-file-l1-1-0.dll` | `CreateDirectoryW` | **33** |  |
| 99 | `api-ms-win-core-timezone-l1-1-0.dll` | `FileTimeToSystemTime` | **33** |  |
| 100 | `ntdll.dll` | `RtlUnsubscribeWnfStateChangeNotification` | **33** |  |

## The shaped candidates, by DLL

### ntdll.dll — 155 candidates

```
  RtlLengthSid                                 fan-in 28
  RtlCopySid                                   fan-in 21
  RtlValidSid                                  fan-in 20
  RtlEqualSid                                  fan-in 19
  memset                                       fan-in 17
  memcpy                                       fan-in 15
  RtlDeriveCapabilitySidsFromName              fan-in 14
  RtlInitAnsiString                            fan-in 14
  RtlSubAuthorityCountSid                      fan-in 12
  memcmp                                       fan-in 12
  RtlCopyUnicodeString                         fan-in 11
  RtlSubAuthoritySid                           fan-in 11
  memmove                                      fan-in 11
  RtlAppendUnicodeStringToString               fan-in 10
  RtlDosPathNameToNtPathName_U                 fan-in 10
  RtlUnicodeStringToInteger                    fan-in 10
  RtlInitializeSid                             fan-in 9
  RtlStringFromGUID                            fan-in 9
  RtlUpcaseUnicodeChar                         fan-in 9
  RtlLengthRequiredSid                         fan-in 8
  strchr                                       fan-in 8
  RtlCreateUnicodeStringFromAsciiz             fan-in 7
  RtlDosPathNameToNtPathName_U_WithStatus      fan-in 7
  RtlFormatCurrentUserKeyPath                  fan-in 7
  RtlxAnsiStringToUnicodeSize                  fan-in 7
  RtlDosPathNameToRelativeNtPathName_U         fan-in 6
  RtlExpandEnvironmentStrings                  fan-in 6
  RtlLengthSecurityDescriptor                  fan-in 6
  RtlLoadString                                fan-in 6
  RtlxUnicodeStringToAnsiSize                  fan-in 6
  wcsstr                                       fan-in 6
  RtlCreateUnicodeString                       fan-in 5
  RtlDestroyEnvironment                        fan-in 5
  RtlDetermineDosPathNameType_U                fan-in 5
  RtlGetAppContainerSidType                    fan-in 5
  RtlSidDominates                              fan-in 5
  memmove_s                                    fan-in 5
  strncmp                                      fan-in 5
  RtlCheckTokenCapability                      fan-in 4
  RtlCheckTokenMembershipEx                    fan-in 4
  RtlCreateServiceSid                          fan-in 4
  RtlDosPathNameToRelativeNtPathName_U_WithStatus fan-in 4
  RtlDuplicateUnicodeString                    fan-in 4
  RtlExpandEnvironmentStrings_U                fan-in 4
  RtlGetAppContainerNamedObjectPath            fan-in 4
  RtlGetFullPathName_U                         fan-in 4
  RtlInitAnsiStringEx                          fan-in 4
  RtlNtPathNameToDosPathName                   fan-in 4
  WinSqmAddToStreamEx                          fan-in 4
  _wcslwr                                      fan-in 4
  strrchr                                      fan-in 4
  wcsnlen                                      fan-in 4
  RtlConvertDeviceFamilyInfoToString           fan-in 3
  RtlConvertSharedToExclusive                  fan-in 3
  RtlGetFullPathName_UEx                       fan-in 3
  RtlIdentifierAuthoritySid                    fan-in 3
  RtlQueryTokenHostIdAsUlong64                 fan-in 3
  RtlRunDecodeUnicodeString                    fan-in 3
  RtlUpcaseUnicodeStringToOemString            fan-in 3
  CsrCaptureMessageMultiUnicodeStringsInPlace  fan-in 2
  ... and 95 more
```

### api-ms-win-security-base-l1-1-0.dll — 28 candidates

```
  GetTokenInformation                          fan-in 85
  GetLengthSid                                 fan-in 46
  CopySid                                      fan-in 34
  RevertToSelf                                 fan-in 34
  CreateWellKnownSid                           fan-in 29
  EqualSid                                     fan-in 28
  IsValidSid                                   fan-in 28
  CheckTokenMembership                         fan-in 26
  GetSidSubAuthority                           fan-in 25
  GetSidSubAuthorityCount                      fan-in 21
  DuplicateTokenEx                             fan-in 20
  AdjustTokenPrivileges                        fan-in 13
  IsWellKnownSid                               fan-in 13
  DuplicateToken                               fan-in 12
  GetSecurityDescriptorLength                  fan-in 11
  GetSidIdentifierAuthority                    fan-in 6
  GetSidLengthRequired                         fan-in 6
  InitializeSid                                fan-in 6
  CreateRestrictedToken                        fan-in 4
  DestroyPrivateObjectSecurity                 fan-in 4
  EqualDomainSid                               fan-in 4
  ImpersonateAnonymousToken                    fan-in 4
  SetTokenInformation                          fan-in 4
  AdjustTokenGroups                            fan-in 2
  ConvertToAutoInheritPrivateObjectSecurity    fan-in 2
  EqualPrefixSid                               fan-in 1
  GetWindowsAccountDomainSid                   fan-in 1
  IsTokenRestricted                            fan-in 1
```

### api-ms-win-crt-private-l1-1-0.dll — 7 candidates

```
  memcpy                                       fan-in 102
  memcmp                                       fan-in 90
  memmove                                      fan-in 86
  strchr                                       fan-in 19
  wcsstr                                       fan-in 18
  strrchr                                      fan-in 13
  strstr                                       fan-in 6
```

### api-ms-win-core-string-l1-1-0.dll — 5 candidates

```
  WideCharToMultiByte                          fan-in 126
  MultiByteToWideChar                          fan-in 89
  CompareStringW                               fan-in 44
  CompareStringEx                              fan-in 9
  GetStringTypeExW                             fan-in 7
```

### api-ms-win-core-file-l1-1-0.dll — 26 candidates

```
  FindClose                                    fan-in 43
  FindFirstFileW                               fan-in 39
  FindNextFileW                                fan-in 37
  CompareFileTime                              fan-in 29
  GetFullPathNameW                             fan-in 28
  FindFirstFileExW                             fan-in 9
  GetFullPathNameA                             fan-in 9
  GetFinalPathNameByHandleW                    fan-in 8
  GetLongPathNameW                             fan-in 8
  GetVolumePathNameW                           fan-in 8
  GetShortPathNameW                            fan-in 5
  FindCloseChangeNotification                  fan-in 4
  FindFirstChangeNotificationW                 fan-in 4
  FindFirstFileA                               fan-in 4
  FindNextChangeNotification                   fan-in 4
  FindNextFileA                                fan-in 4
  FindFirstVolumeW                             fan-in 3
  FindNextVolumeW                              fan-in 3
  FindVolumeClose                              fan-in 3
  FindFirstFileNameW                           fan-in 2
  FindNextFileNameW                            fan-in 2
  FindFirstChangeNotificationA                 fan-in 1
  FindFirstFileExA                             fan-in 1
  GetFinalPathNameByHandleA                    fan-in 1
  GetLogicalDriveStringsW                      fan-in 1
  GetLongPathNameA                             fan-in 1
```

### msvcrt.dll — 20 candidates

```
  memset                                       fan-in 36
  memcpy                                       fan-in 35
  memcpy_s                                     fan-in 29
  memcmp                                       fan-in 25
  memmove                                      fan-in 25
  memmove_s                                    fan-in 19
  wcsstr                                       fan-in 10
  wcsnlen                                      fan-in 6
  strrchr                                      fan-in 5
  _wcslwr                                      fan-in 3
  strchr                                       fan-in 3
  strstr                                       fan-in 3
  wcstod                                       fan-in 3
  _wcsdup                                      fan-in 2
  strncmp                                      fan-in 2
  strnlen                                      fan-in 2
  _strlwr                                      fan-in 1
  mbstowcs                                     fan-in 1
  wcsftime                                     fan-in 1
  wcstombs                                     fan-in 1
```

### api-ms-win-core-localization-l1-2-0.dll — 9 candidates

```
  FormatMessageW                               fan-in 130
  LCMapStringW                                 fan-in 22
  FormatMessageA                               fan-in 12
  LCMapStringEx                                fan-in 7
  FindNLSString                                fan-in 5
  FindNLSStringEx                              fan-in 5
  LCMapStringA                                 fan-in 2
  GetFileMUIPath                               fan-in 1
  IsNLSDefinedString                           fan-in 1
```

### api-ms-win-core-winrt-string-l1-1-0.dll — 14 candidates

```
  WindowsCreateStringReference                 fan-in 28
  WindowsDeleteString                          fan-in 25
  WindowsGetStringRawBuffer                    fan-in 25
  WindowsCreateString                          fan-in 22
  WindowsIsStringEmpty                         fan-in 17
  WindowsStringHasEmbeddedNull                 fan-in 17
  WindowsDuplicateString                       fan-in 13
  WindowsCompareStringOrdinal                  fan-in 7
  WindowsGetStringLen                          fan-in 5
  WindowsDeleteStringBuffer                    fan-in 3
  WindowsPreallocateStringBuffer               fan-in 3
  WindowsPromoteStringBuffer                   fan-in 3
  WindowsConcatString                          fan-in 1
  WindowsSubstringWithSpecifiedLength          fan-in 1
```

### api-ms-win-core-shlwapi-legacy-l1-1-0.dll — 59 candidates

```
  PathFileExistsW                              fan-in 16
  PathGetDriveNumberW                          fan-in 9
  PathIsUNCW                                   fan-in 8
  PathIsRelativeW                              fan-in 6
  PathParseIconLocationW                       fan-in 6
  PathRemoveFileSpecW                          fan-in 6
  PathSkipRootW                                fan-in 6
  PathStripToRootW                             fan-in 5
  PathUnExpandEnvStringsW                      fan-in 5
  PathIsRootW                                  fan-in 4
  PathIsUNCServerShareW                        fan-in 4
  PathIsUNCServerW                             fan-in 4
  PathMatchSpecW                               fan-in 4
  PathUnquoteSpacesW                           fan-in 4
  SHExpandEnvironmentStringsW                  fan-in 4
  IsCharSpaceW                                 fan-in 3
  PathAppendW                                  fan-in 3
  PathCombineW                                 fan-in 3
  PathGetArgsW                                 fan-in 3
  PathIsValidCharW                             fan-in 3
  PathMatchSpecExW                             fan-in 3
  PathRelativePathToW                          fan-in 3
  PathAppendA                                  fan-in 2
  PathFileExistsA                              fan-in 2
  PathGetArgsA                                 fan-in 2
  PathGetCharTypeW                             fan-in 2
  PathGetDriveNumberA                          fan-in 2
  PathIsRootA                                  fan-in 2
  PathRemoveFileSpecA                          fan-in 2
  PathUnExpandEnvStringsA                      fan-in 2
  SHExpandEnvironmentStringsA                  fan-in 2
  IsCharBlankW                                 fan-in 1
  IsCharCntrlW                                 fan-in 1
  IsCharDigitW                                 fan-in 1
  IsCharPunctW                                 fan-in 1
  IsCharSpaceA                                 fan-in 1
  IsCharXDigitW                                fan-in 1
  PathAddBackslashA                            fan-in 1
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
  PathSearchAndQualifyW                        fan-in 1
  PathSkipRootA                                fan-in 1
  PathStripToRootA                             fan-in 1
  PathUnquoteSpacesA                           fan-in 1
  SHTruncateString                             fan-in 1
```

### api-ms-win-crt-string-l1-1-0.dll — 7 candidates

```
  memset                                       fan-in 106
  strncmp                                      fan-in 15
  wcsnlen                                      fan-in 13
  memmove_s                                    fan-in 5
  strnlen                                      fan-in 4
  strncpy                                      fan-in 2
  _wcsdup                                      fan-in 1
```

### api-ms-win-core-shlwapi-obsolete-l1-1-0.dll — 37 candidates

```
  QISearch                                     fan-in 12
  StrCmpIW                                     fan-in 8
  StrCmpNICW                                   fan-in 8
  StrCmpICW                                    fan-in 7
  StrCmpNIW                                    fan-in 6
  StrCmpNW                                     fan-in 6
  StrCmpW                                      fan-in 6
  StrDupA                                      fan-in 6
  StrDupW                                      fan-in 6
  StrToIntExW                                  fan-in 6
  StrToIntW                                    fan-in 6
  StrCmpCW                                     fan-in 5
  StrCmpICA                                    fan-in 5
  StrCmpLogicalW                               fan-in 4
  StrCmpNCW                                    fan-in 4
  StrStrA                                      fan-in 4
  StrStrIA                                     fan-in 4
  StrStrNIW                                    fan-in 4
  StrToIntA                                    fan-in 4
  StrCmpNA                                     fan-in 3
  StrCmpNCA                                    fan-in 3
  StrCmpNIA                                    fan-in 3
  StrCmpNICA                                   fan-in 3
  StrToInt64ExW                                fan-in 3
  StrChrIA                                     fan-in 2
  StrCmpCA                                     fan-in 2
  StrCpyNXW                                    fan-in 2
  StrRChrIA                                    fan-in 2
  StrRStrIA                                    fan-in 2
  StrToInt64ExA                                fan-in 2
  StrCSpnIA                                    fan-in 1
  StrCatChainW                                 fan-in 1
  StrCpyNXA                                    fan-in 1
  StrIsIntlEqualA                              fan-in 1
  StrIsIntlEqualW                              fan-in 1
  StrStrNW                                     fan-in 1
  StrToIntExA                                  fan-in 1
```

### api-ms-win-core-debug-l1-1-0.dll — 2 candidates

```
  OutputDebugStringW                           fan-in 119
  OutputDebugStringA                           fan-in 20
```

### api-ms-win-core-util-l1-1-0.dll — 2 candidates

```
  EncodePointer                                fan-in 71
  DecodePointer                                fan-in 66
```

### gdi32.dll — 54 candidates

```
  GetDIBits                                    fan-in 7
  SetStretchBltMode                            fan-in 6
  StretchDIBits                                fan-in 6
  CreateCompatibleBitmap                       fan-in 5
  StretchBlt                                   fan-in 5
  SetDIBits                                    fan-in 4
  CreateBitmap                                 fan-in 3
  CreateDIBitmap                               fan-in 3
  GetBitmapBits                                fan-in 3
  BeginPath                                    fan-in 2
  CopyEnhMetaFileW                             fan-in 2
  CopyMetaFileW                                fan-in 2
  EndPath                                      fan-in 2
  Escape                                       fan-in 2
  GdiGetCharDimensions                         fan-in 2
  GdiGradientFill                              fan-in 2
  GetCharABCWidthsW                            fan-in 2
  GetCharWidthInfo                             fan-in 2
  GetTextCharset                               fan-in 2
  GetTextCharsetInfo                           fan-in 2
  SelectClipPath                               fan-in 2
  SetMetaFileBitsEx                            fan-in 2
  TranslateCharsetInfo                         fan-in 2
  AbortPath                                    fan-in 1
  ClearBitmapAttributes                        fan-in 1
  CreateBitmapIndirect                         fan-in 1
  ExtEscape                                    fan-in 1
  FillPath                                     fan-in 1
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
  GetCharWidthA                                fan-in 1
  GetCharWidthW                                fan-in 1
  GetEnhMetaFileBits                           fan-in 1
  GetMetaFileBitsEx                            fan-in 1
  GetTextCharacterExtra                        fan-in 1
  GetWinMetaFileBits                           fan-in 1
  PathToRegion                                 fan-in 1
  SetBitmapAttributes                          fan-in 1
  SetBitmapBits                                fan-in 1
  SetBitmapDimensionEx                         fan-in 1
  SetEnhMetaFileBits                           fan-in 1
  SetPolyFillMode                              fan-in 1
  SetTextCharacterExtra                        fan-in 1
  SetWinMetaFileBits                           fan-in 1
  StrokeAndFillPath                            fan-in 1
  StrokePath                                   fan-in 1
```

### kernelbase.dll — 67 candidates

```
  FindPackagesByPackageFamily                  fan-in 5
  GetStagedPackagePathByFullName2              fan-in 5
  GetStagedPackagePathByFullName               fan-in 4
  ParseApplicationUserModelId                  fan-in 4
  GetApplicationUserModelIdFromToken           fan-in 3
  GetPackageFamilyNameFromToken                fan-in 3
  GetSystemMetadataPathForPackage              fan-in 3
  lstrcmpW                                     fan-in 3
  lstrcmpiW                                    fan-in 3
  lstrlenW                                     fan-in 3
  AppContainerRegisterSid                      fan-in 2
  AppContainerUnregisterSid                    fan-in 2
  BaseFormatObjectAttributes                   fan-in 2
  CompareStringA                               fan-in 2
  GetPackagePathByFullName                     fan-in 2
  GetPackagePathByFullName2                    fan-in 2
  LoadStringBaseExW                            fan-in 2
  SHLoadIndirectStringInternal                 fan-in 2
  AppContainerDeriveSidFromMoniker             fan-in 1
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
  NotifyRedirectedStringChange                 fan-in 1
  OpenStateExplicitForUserSid                  fan-in 1
  OpenStateExplicitForUserSidString            fan-in 1
  PackageSidFromFamilyName                     fan-in 1
  PackageSidFromProductId                      fan-in 1
  ParseApplicationUserModelIdA                 fan-in 1
  PrivCopyFileExW                              fan-in 1
  ... and 7 more
```

### api-ms-win-core-libraryloader-l1-2-0.dll — 3 candidates

```
  LoadStringW                                  fan-in 46
  FindResourceExW                              fan-in 31
  LoadStringA                                  fan-in 6
```

### api-ms-win-core-processenvironment-l1-1-0.dll — 6 candidates

```
  ExpandEnvironmentStringsW                    fan-in 53
  SearchPathW                                  fan-in 14
  ExpandEnvironmentStringsA                    fan-in 7
  GetEnvironmentStringsW                       fan-in 6
  GetEnvironmentStrings                        fan-in 1
  SetEnvironmentStringsW                       fan-in 1
```

### api-ms-win-core-string-obsolete-l1-1-0.dll — 5 candidates

```
  lstrcmpW                                     fan-in 24
  lstrcmpiW                                    fan-in 20
  lstrlenW                                     fan-in 16
  lstrcmpA                                     fan-in 10
  lstrcmpiA                                    fan-in 6
```

### api-ms-win-core-url-l1-1-0.dll — 30 candidates

```
  UrlEscapeW                                   fan-in 7
  PathCreateFromUrlW                           fan-in 6
  ParseURLW                                    fan-in 4
  PathIsURLW                                   fan-in 4
  UrlCanonicalizeW                             fan-in 4
  UrlCombineW                                  fan-in 4
  UrlCompareW                                  fan-in 4
  UrlCreateFromPathW                           fan-in 4
  UrlGetPartW                                  fan-in 4
  UrlIsW                                       fan-in 4
  UrlApplySchemeW                              fan-in 3
  UrlGetLocationW                              fan-in 3
  ParseURLA                                    fan-in 2
  PathCreateFromUrlA                           fan-in 2
  UrlFixupW                                    fan-in 2
  PathIsURLA                                   fan-in 1
  UrlApplySchemeA                              fan-in 1
  UrlCanonicalizeA                             fan-in 1
  UrlCombineA                                  fan-in 1
  UrlCompareA                                  fan-in 1
  UrlCreateFromPathA                           fan-in 1
  UrlEscapeA                                   fan-in 1
  UrlGetLocationA                              fan-in 1
  UrlGetPartA                                  fan-in 1
  UrlHashW                                     fan-in 1
  UrlIsA                                       fan-in 1
  UrlIsNoHistoryA                              fan-in 1
  UrlIsNoHistoryW                              fan-in 1
  UrlIsOpaqueA                                 fan-in 1
  UrlIsOpaqueW                                 fan-in 1
```

### user32.dll — 37 candidates

```
  CopyImage                                    fan-in 5
  DestroyIcon                                  fan-in 5
  DestroyWindow                                fan-in 4
  EqualRect                                    fan-in 4
  FillRect                                     fan-in 4
  CopyIcon                                     fan-in 3
  CopyRect                                     fan-in 3
  FindWindowW                                  fan-in 3
  IsClipboardFormatAvailable                   fan-in 3
  AreDpiAwarenessContextsEqual                 fan-in 2
  CopyAcceleratorTableW                        fan-in 2
  CountClipboardFormats                        fan-in 2
  DestroyMenu                                  fan-in 2
  FindWindowExW                                fan-in 2
  GetWindowTextLengthW                         fan-in 2
  RegisterClipboardFormatW                     fan-in 2
  ValidateRect                                 fan-in 2
  CharToOemBuffW                               fan-in 1
  CharUpperA                                   fan-in 1
  CharUpperW                                   fan-in 1
  DdeCreateStringHandleW                       fan-in 1
  DdeQueryConvInfo                             fan-in 1
  DdeQueryStringW                              fan-in 1
  DestroyAcceleratorTable                      fan-in 1
  DestroyCaret                                 fan-in 1
  DestroyCursor                                fan-in 1
  EnumClipboardFormats                         fan-in 1
  GetClipboardFormatNameA                      fan-in 1
  GetClipboardFormatNameW                      fan-in 1
  GetMenuStringW                               fan-in 1
  GrayStringW                                  fan-in 1
  IsServerSideWindow                           fan-in 1
  IsWindowInDestroy                            fan-in 1
  LoadBitmapW                                  fan-in 1
  OemToCharBuffW                               fan-in 1
  RegisterClipboardFormatA                     fan-in 1
  ShutdownBlockReasonDestroy                   fan-in 1
```

### api-ms-win-core-string-l2-1-0.dll — 8 candidates

```
  CharNextW                                    fan-in 16
  CharLowerW                                   fan-in 11
  CharPrevW                                    fan-in 6
  IsCharAlphaW                                 fan-in 6
  CharUpperW                                   fan-in 5
  IsCharAlphaNumericW                          fan-in 5
  IsCharLowerW                                 fan-in 2
  IsCharUpperW                                 fan-in 2
```

### api-ms-win-eventing-classicprovider-l1-1-0.dll — 2 candidates

```
  UnregisterTraceGuids                         fan-in 23
  RegisterTraceGuidsW                          fan-in 19
```

### api-ms-win-core-winrt-error-l1-1-0.dll — 2 candidates

```
  SetRestrictedErrorInfo                       fan-in 23
  GetRestrictedErrorInfo                       fan-in 16
```

### api-ms-win-core-com-l1-1-0.dll — 6 candidates

```
  CreateStreamOnHGlobal                        fan-in 16
  CLSIDFromString                              fan-in 10
  StringFromCLSID                              fan-in 8
  GetHGlobalFromStream                         fan-in 2
  PropVariantCopy                              fan-in 2
  StringFromIID                                fan-in 1
```

### api-ms-win-core-path-l1-1-0.dll — 7 candidates

```
  PathCchCombine                               fan-in 11
  PathCchAppend                                fan-in 10
  PathCchSkipRoot                              fan-in 8
  PathCchCanonicalize                          fan-in 4
  PathCchStripToRoot                           fan-in 2
  PathIsUNCEx                                  fan-in 2
  PathCchStripPrefix                           fan-in 1
```

### kernel32.dll — 26 candidates

```
  MultiByteToWideChar                          fan-in 3
  DecodePointer                                fan-in 2
  EncodePointer                                fan-in 2
  FindClose                                    fan-in 2
  FindFirstFileExW                             fan-in 2
  FindNextFileW                                fan-in 2
  FormatMessageW                               fan-in 2
  OutputDebugStringW                           fan-in 2
  WideCharToMultiByte                          fan-in 2
  CompareFileTime                              fan-in 1
  CompareStringW                               fan-in 1
  ConvertNLSDayOfWeekToWin32DayOfWeek          fan-in 1
  CopyFileExW                                  fan-in 1
  ExpandEnvironmentStringsA                    fan-in 1
  ExpandEnvironmentStringsW                    fan-in 1
  FindResourceExW                              fan-in 1
  FlushInstructionCache                        fan-in 1
  GetCalendarDateFormat                        fan-in 1
  GetEnvironmentStringsW                       fan-in 1
  GetFileMUIPath                               fan-in 1
  GetFinalPathNameByHandleW                    fan-in 1
  GetFullPathNameW                             fan-in 1
  GetLongPathNameW                             fan-in 1
  GetVolumePathNameW                           fan-in 1
  LCMapStringW                                 fan-in 1
  SearchPathW                                  fan-in 1
```

### api-ms-win-core-processthreads-l1-1-0.dll — 1 candidates

```
  SetThreadToken                               fan-in 34
```

### msvcp_win.dll — 4 candidates

```
  _Mtx_destroy_in_situ                         fan-in 20
  _Cnd_destroy_in_situ                         fan-in 12
  _Strcoll                                     fan-in 1
  _Strxfrm                                     fan-in 1
```

### api-ms-win-core-datetime-l1-1-0.dll — 4 candidates

```
  GetDateFormatW                               fan-in 12
  GetTimeFormatW                               fan-in 11
  GetTimeFormatA                               fan-in 4
  GetDateFormatA                               fan-in 3
```

### api-ms-win-core-crt-l1-1-0.dll — 13 candidates

```
  memset                                       fan-in 5
  memcpy                                       fan-in 4
  memcmp                                       fan-in 3
  memcpy_s                                     fan-in 3
  memmove                                      fan-in 3
  memmove_s                                    fan-in 3
  wcsstr                                       fan-in 2
  strchr                                       fan-in 1
  strncmp                                      fan-in 1
  strnlen                                      fan-in 1
  strrchr                                      fan-in 1
  strstr                                       fan-in 1
  wcsnlen                                      fan-in 1
```

### api-ms-win-core-stringansi-l1-1-0.dll — 12 candidates

```
  CharNextA                                    fan-in 7
  CharPrevA                                    fan-in 4
  CharLowerA                                   fan-in 3
  CharUpperBuffA                               fan-in 3
  CharUpperA                                   fan-in 2
  IsCharAlphaNumericA                          fan-in 2
  CharLowerBuffA                               fan-in 1
  CharNextExA                                  fan-in 1
  CharPrevExA                                  fan-in 1
  IsCharAlphaA                                 fan-in 1
  IsCharLowerA                                 fan-in 1
  IsCharUpperA                                 fan-in 1
```

### rpcrt4.dll — 8 candidates

```
  UuidCreate                                   fan-in 10
  UuidToStringW                                fan-in 3
  MesDecodeBufferHandleCreate                  fan-in 2
  MesEncodeFixedBufferHandleCreate             fan-in 2
  MesDecodeIncrementalHandleCreate             fan-in 1
  MesEncodeIncrementalHandleCreate             fan-in 1
  UuidEqual                                    fan-in 1
  UuidHash                                     fan-in 1
```

### api-ms-win-core-winrt-error-l1-1-1.dll — 1 candidates

```
  RoGetMatchingRestrictedErrorInfo             fan-in 20
```

### shcore.dll — 10 candidates

```
  IStream_Read                                 fan-in 4
  IStream_Size                                 fan-in 4
  IStream_Reset                                fan-in 3
  CreateStreamOverRandomAccessStream           fan-in 2
  SHCreateMemStream                            fan-in 2
  CreateRandomAccessStreamOverStream           fan-in 1
  IStream_Copy                                 fan-in 1
  SHCreateStreamOnFileEx                       fan-in 1
  SHCreateStreamOnFileW                        fan-in 1
  SHStrDupW                                    fan-in 1
```

### api-ms-win-core-file-l1-2-0.dll — 2 candidates

```
  GetTempPathW                                 fan-in 16
  GetVolumePathNamesForVolumeNameW             fan-in 3
```

### api-ms-win-core-string-l2-1-1.dll — 1 candidates

```
  SHLoadIndirectString                         fan-in 16
```

### api-ms-win-core-privateprofile-l1-1-0.dll — 4 candidates

```
  GetPrivateProfileStringW                     fan-in 5
  WritePrivateProfileStringW                   fan-in 5
  GetProfileStringW                            fan-in 3
  GetPrivateProfileStringA                     fan-in 1
```

### pimstore.dll — 9 candidates

```
  GetSortBy                                    fan-in 3
  CopyCEPROPVAL                                fan-in 2
  FindAllMatchingContactsEx                    fan-in 2
  SetSortBy                                    fan-in 2
  FindAllMatchingAggregates                    fan-in 1
  FindAllMatchingContactsEx2                   fan-in 1
  FindMatchingContactEx                        fan-in 1
  FindMatchingContactEx2                       fan-in 1
  PimBinaryBodyToString                        fan-in 1
```

### api-ms-win-core-localization-obsolete-l1-2-0.dll — 3 candidates

```
  CompareStringA                               fan-in 7
  GetNumberFormatW                             fan-in 5
  GetStringTypeA                               fan-in 1
```

### api-ms-win-core-localization-l2-1-0.dll — 8 candidates

```
  GetNumberFormatEx                            fan-in 3
  EnumDateFormatsExEx                          fan-in 2
  EnumTimeFormatsEx                            fan-in 2
  GetCurrencyFormatEx                          fan-in 2
  EnumDateFormatsExW                           fan-in 1
  EnumDateFormatsW                             fan-in 1
  EnumTimeFormatsW                             fan-in 1
  GetCurrencyFormatW                           fan-in 1
```

### vcruntime140_app.dll — 6 candidates

```
  memcpy                                       fan-in 3
  memmove                                      fan-in 3
  memcmp                                       fan-in 2
  memset                                       fan-in 2
  strchr                                       fan-in 1
  wcsstr                                       fan-in 1
```

### api-ms-win-core-file-l2-1-2.dll — 1 candidates

```
  CopyFileW                                    fan-in 11
```

### api-ms-win-core-libraryloader-l1-2-1.dll — 1 candidates

```
  FindResourceW                                fan-in 11
```

### bcrypt.dll — 6 candidates

```
  BCryptCreateHash                             fan-in 2
  BCryptDestroyHash                            fan-in 2
  BCryptDestroyKey                             fan-in 2
  BCryptFinishHash                             fan-in 2
  BCryptHashData                               fan-in 2
  BCryptDuplicateHash                          fan-in 1
```

### api-ms-win-core-datetime-l1-1-1.dll — 2 candidates

```
  GetDateFormatEx                              fan-in 5
  GetTimeFormatEx                              fan-in 5
```

### api-ms-win-core-atoms-l1-1-0.dll — 4 candidates

```
  FindAtomW                                    fan-in 4
  GlobalFindAtomW                              fan-in 3
  GlobalFindAtomA                              fan-in 2
  FindAtomA                                    fan-in 1
```

### phoneutil.dll — 5 candidates

```
  ComparePhoneNumbers                          fan-in 2
  GetDialStringFromTelUri                      fan-in 2
  GetTelUriFromDialString                      fan-in 2
  IsDialableChar                               fan-in 2
  Phone_FmtText_NonDialerFormat                fan-in 2
```

### api-ms-win-core-file-l1-2-3.dll — 2 candidates

```
  GetTempPath2W                                fan-in 8
  GetTempPath2A                                fan-in 1
```

### api-ms-win-core-file-l2-1-0.dll — 2 candidates

```
  CopyFile2                                    fan-in 4
  CopyFileExW                                  fan-in 4
```

### api-ms-win-core-kernel32-legacy-l1-1-0.dll — 5 candidates

```
  FindResourceExA                              fan-in 3
  GetShortPathNameA                            fan-in 2
  CopyFileA                                    fan-in 1
  CopyFileW                                    fan-in 1
  GetStringTypeExA                             fan-in 1
```

### api-ms-win-core-winrt-l1-1-0.dll — 1 candidates

```
  RoRevokeActivationFactories                  fan-in 7
```

### api-ms-win-security-base-l1-2-0.dll — 2 candidates

```
  CheckTokenMembershipEx                       fan-in 5
  CheckTokenCapability                         fan-in 2
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

### api-ms-win-core-wow64-l1-1-0.dll — 1 candidates

```
  Wow64RevertWow64FsRedirection                fan-in 6
```

### api-ms-win-core-file-l1-2-2.dll — 2 candidates

```
  GetTempPathA                                 fan-in 5
  FindFirstStreamW                             fan-in 1
```

### api-ms-win-core-normalization-l1-1-0.dll — 3 candidates

```
  GetStringScripts                             fan-in 4
  IsNormalizedString                           fan-in 1
  NormalizeString                              fan-in 1
```

### api-ms-win-core-processenvironment-l1-2-0.dll — 3 candidates

```
  SearchPathA                                  fan-in 4
  NeedCurrentDirectoryForExePathA              fan-in 1
  NeedCurrentDirectoryForExePathW              fan-in 1
```

### windows.storage.dll — 5 candidates

```
  SHGetKnownFolderPath                         fan-in 2
  ILIsEqual                                    fan-in 1
  PathSeperateArgs                             fan-in 1
  SHParseDisplayName                           fan-in 1
  STORAGE_PathIsEqualOrSubFolderOfKnownFolders fan-in 1
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

### messagingdatamodel2.dll — 6 candidates

```
  Messaging_FormatRecipientFromAggregate       fan-in 1
  Messaging_GetRecipientsString                fan-in 1
  Messaging_GetSmsCharacterCount               fan-in 1
  Messaging_IsDataRoamingRestrictionActive     fan-in 1
  Messaging_IsThreadedByRemoteConversationId   fan-in 1
  Messaging_IsVoiceRoamingRestrictionActive    fan-in 1
```

### ntlmshared.dll — 6 candidates

```
  MsvpAvlLen                                   fan-in 1
  MsvpAvlToString                              fan-in 1
  MsvpCompareCredentials                       fan-in 1
  MsvpComputeSaltedHashedPassword              fan-in 1
  MsvpPasswordValidate                         fan-in 1
  MsvpValidateSupplementalCreds                fan-in 1
```

### userdatatypehelperutil.dll — 6 candidates

```
  CopyStream                                   fan-in 1
  CreateWrapFileStreamFromDssToken             fan-in 1
  FormatPoomIdToString                         fan-in 1
  ReadStreamContent                            fan-in 1
  StreamFromStringW                            fan-in 1
  StringToBytes                                fan-in 1
```

### api-ms-win-core-localization-private-l1-1-0.dll — 3 candidates

```
  LoadStringByReference                        fan-in 3
  _AddMUIStringToCache                         fan-in 1
  _OpenMuiStringCache                          fan-in 1
```

### api-ms-win-core-sidebyside-l1-1-0.dll — 2 candidates

```
  FindActCtxSectionGuid                        fan-in 3
  FindActCtxSectionStringW                     fan-in 2
```

### api-ms-win-core-calendar-l1-1-0.dll — 3 candidates

```
  ConvertCalDateTimeToSystemTime               fan-in 2
  ConvertSystemTimeToCalDateTime               fan-in 2
  GetCalendarDateFormatEx                      fan-in 1
```

### api-ms-win-core-psm-key-l1-1-0.dll — 1 candidates

```
  PsmGetKeyFromToken                           fan-in 4
```

### api-ms-win-security-sddl-l1-1-0.dll — 2 candidates

```
  ConvertStringSecurityDescriptorToSecurityDescriptorW fan-in 3
  ConvertSecurityDescriptorToStringSecurityDescriptorW fan-in 1
```

### api-ms-win-appmodel-runtime-l1-1-1.dll — 4 candidates

```
  GetApplicationUserModelIdFromToken           fan-in 1
  GetPackageFullNameFromToken                  fan-in 1
  GetPackagePathByFullName                     fan-in 1
  ParseApplicationUserModelId                  fan-in 1
```

### api-ms-win-security-audit-l1-1-1.dll — 4 candidates

```
  AuditEnumerateCategories                     fan-in 1
  AuditEnumerateSubCategories                  fan-in 1
  AuditLookupCategoryNameW                     fan-in 1
  AuditLookupSubCategoryNameW                  fan-in 1
```

### api-ms-win-security-cryptoapi-l1-1-0.dll — 4 candidates

```
  CryptCreateHash                              fan-in 1
  CryptDestroyHash                             fan-in 1
  CryptGetHashParam                            fan-in 1
  CryptHashData                                fan-in 1
```

### coremessaging.dll — 4 candidates

```
  CoreUICallCreateConversationHost             fan-in 1
  CoreUICreateAnonymousStream                  fan-in 1
  MsgStringCreateShared                        fan-in 1
  MsgStringCreateStack                         fan-in 1
```

### api-ms-win-eventing-obsolete-l1-1-0.dll — 1 candidates

```
  RegisterTraceGuidsA                          fan-in 3
```

### userdataplatformhelperutil.dll — 3 candidates

```
  CreateKnownFolderPath                        fan-in 1
  GetContentTypeFromFilePath                   fan-in 1
  GetUserTokenFromContext                      fan-in 1
```

### ws2_32.dll — 3 candidates

```
  WPUGetProviderPathEx                         fan-in 1
  WSCGetProviderPath                           fan-in 1
  WahDestroyHandleContextTable                 fan-in 1
```

### api-ms-win-core-datetime-l1-1-2.dll — 1 candidates

```
  GetDurationFormatEx                          fan-in 2
```

### api-ms-win-core-processthreads-l1-1-1.dll — 1 candidates

```
  FlushInstructionCache                        fan-in 2
```

### api-ms-win-devices-query-l1-1-0.dll — 1 candidates

```
  DevFindProperty                              fan-in 2
```

### advapi32.dll — 2 candidates

```
  ConvertStringSecurityDescriptorToSecurityDescriptorW fan-in 1
  IsValidSid                                   fan-in 1
```

### api-ms-win-core-comm-l1-1-0.dll — 2 candidates

```
  EscapeCommFunction                           fan-in 1
  TransmitCommChar                             fan-in 1
```

### api-ms-win-core-fibers-l2-1-0.dll — 2 candidates

```
  ConvertFiberToThread                         fan-in 1
  ConvertThreadToFiber                         fan-in 1
```

### api-ms-win-core-misc-l1-1-0.dll — 2 candidates

```
  lstrcmpiA                                    fan-in 1
  lstrlenW                                     fan-in 1
```

### api-ms-win-crt-convert-l1-1-0.dll — 2 candidates

```
  strtod                                       fan-in 1
  strtof                                       fan-in 1
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

### api-ms-win-shcore-stream-winrt-l1-1-0.dll — 2 candidates

```
  CreateRandomAccessStreamOverStream           fan-in 1
  CreateStreamOverRandomAccessStream           fan-in 1
```

### shell32.dll — 2 candidates

```
  SHGetPathFromIDListW                         fan-in 1
  SHParseDisplayName                           fan-in 1
```

### urlmon.dll — 2 candidates

```
  UrlMkGetSessionOption                        fan-in 1
  UrlMkSetSessionOption                        fan-in 1
```

### api-ms-win-core-appcompat-l1-1-0.dll — 1 candidates

```
  BaseIsAppcompatInfrastructureDisabled        fan-in 1
```

### api-ms-win-core-fibers-l2-1-1.dll — 1 candidates

```
  ConvertThreadToFiberEx                       fan-in 1
```

### api-ms-win-core-kernel32-private-l1-1-1.dll — 1 candidates

```
  PrivCopyFileExW                              fan-in 1
```

### api-ms-win-core-xstate-l2-1-0.dll — 1 candidates

```
  CopyContext                                  fan-in 1
```

### api-ms-win-crt-stdio-l1-1-0.dll — 1 candidates

```
  _get_stream_buffer_pointers                  fan-in 1
```

### api-ms-win-crt-time-l1-1-0.dll — 1 candidates

```
  _Strftime                                    fan-in 1
```

### api-ms-win-eventing-controller-l1-1-0.dll — 1 candidates

```
  EnumerateTraceGuidsEx                        fan-in 1
```

### api-ms-win-security-appcontainer-l1-1-0.dll — 1 candidates

```
  GetAppContainerNamedObjectPath               fan-in 1
```

### api-ms-win-security-audit-l1-1-0.dll — 1 candidates

```
  AuditComputeEffectivePolicyBySid             fan-in 1
```

### api-ms-win-shcore-stream-l1-1-0.dll — 1 candidates

```
  SHCreateMemStream                            fan-in 1
```

### api-ms-win-shell-namespace-l1-1-0.dll — 1 candidates

```
  SHParseDisplayName                           fan-in 1
```

### cdp.dll — 1 candidates

```
  CDPCreateAppRegistrationManagerInternal      fan-in 1
```

### cemapi.dll — 1 candidates

```
  MAPI_CompareEntryIDs                         fan-in 1
```

### esent.dll — 1 candidates

```
  JetConvertDDLA                               fan-in 1
```

### imagehlp.dll — 1 candidates

```
  SymGetModuleBase64                           fan-in 1
```

### ncrypt.dll — 1 candidates

```
  NCryptSignHash                               fan-in 1
```

### propsys.dll — 1 candidates

```
  PSPropertyKeyFromString                      fan-in 1
```

### staterepository.core.dll — 1 candidates

```
  sqlite3_result_error_nomem                   fan-in 1
```

### win32u.dll — 1 candidates

```
  ABI_Get_tooltipStrings                       fan-in 1
```

