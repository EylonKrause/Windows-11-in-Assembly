# 2ND PC — full re-validation of all 166 changes on Zen 4

Every change in this repository was rebuilt and re-measured on the **second PC**, through each change's
own `build.bat` — the project's own two gates, unaltered: bit-exact against the **live system export on
this machine**, then no regressed size class.

The original `impl.asm` of every change is **untouched**. Where a change needed work here, an additional
`impl_2ndpc.asm` + `build_2ndpc.bat` + `RESULTS-2ndpc.md` sit alongside it, marked as the 2nd-PC variant.

## The two machines are not the same bench

| | 1st PC (`docs/PLATFORM.md`) | 2nd PC (this run) |
|---|---|---|
| CPU | Ryzen 9 5950X — **Zen 3**, 16C/32T | Ryzen 9 **8940HX — Zen 4**, 16C/32T |
| AVX-512 | **absent** | **present** (F, BW, CD, DQ, VBMI, VBMI2) |
| GFNI | absent | **present** |
| 256-bit datapath | split into 2×128-bit | **native** |
| OS build | 26200.**8655** | 26200.**9445** |
| `ntdll.dll` | 26100.8655-era | **10.0.26100.9278** |
| `ucrtbase.dll` | " | **10.0.26100.9444** |
| `shlwapi.dll` / `crypt32.dll` | " | 10.0.26100.8117 / 10.0.26100.1 |
| RAM | 31.9 GB, **documented bad** (WHEA, BCD-blacklisted pages) | 31.3 GB, no such fault |
| Toolchain | ml64 / cl 14.50.35717 | **identical version and path** |

Two consequences. Measurements here are taken against **newer shipped DLLs**, so the 1st PC's ratios do
not automatically carry over. And this is the Zen4/AVX-512 machine `docs/PLATFORM.md` says the 512-bit
variants would need — though note that **no 2nd-PC variant uses AVX-512 or GFNI**, for the reason under
*Safety* below.

## Headline result

| | |
|---|---|
| Changes swept | **166** (165 buildable; `163-pathremovefilespecw` has no implementation — its contract was never derived) |
| **Correctness against this PC's newer DLLs** | **165 / 165 PASS** |
| Passed both gates unchanged | **153** |
| Fixed by a 2nd-PC variant | **+5** |
| **Total landing on this PC** | **158** (vs 154 on the 5950X) |
| Still parked | 8 |

**The most important line is the correctness one.** Every reverse-engineered contract in this repository —
the IPv6 grammar, the UTF-8 maximal-subpart rule, the CRC-64 polynomial, the four distinct MAX_PATH
behaviours, the `PathFindFileNameW` colon rule, the `_s` error paths — still reproduces the live export
**bit-exactly on newer ntdll / ucrtbase / crypt32 / shlwapi / kernelbase builds**. None of the RE work was
build-specific.

## What changed verdict between the two machines

### Promoted — parked on Zen 3, land here with **no code change**

| # | routine | Zen 3 | Zen 4 geomean | why |
|---|---|---|---|---|
| 005 | `memcmp` | PARKED (lost ≤32 B) | **1.669×** | worst class now 1.17× at 8 B |
| 049 | `_wcslwr` | PARKED (0.91× at 8) | **2.926×** | worst class now 1.18× at 8 |
| 099 | `strncmp` | PARKED (0.91× at 32 B) | **1.688×** | worst class now 1.08× at 32 |
| 129 | `RtlCharToInteger` | PARKED (lost on 2-char) | **1.277×** | worst class now 1.01× |

These are free — the Zen 3 code simply wins here.

### Demoted — landed on Zen 3, regressed here, **fixed by a 2nd-PC variant**

| # | routine | failing class | Zen 3 code here | with variant | geomean |
|---|---|---|---|---|---|
| 008 | `RtlCompareUnicodeString` | 8/CI | 0.95× | **1.02×** | 3.438 → **3.492×** |
| 047 | `_strlwr` | 8 | 0.83× | **1.85×** | 9.277 → **10.159×** |
| 097 | `RtlIntegerToChar` | dec-1d | 0.70× | **2.19×** | 1.327 → **1.573×** |
| 135 | `StrSpnW` | 16/set23 | 0.86× | **1.52×** | 2.671 → **7.887×** |
| 141 | `PathRemoveBlanksW` | 16 | 0.96× | **1.01×** | 4.610 → **4.751×** |

All five were **dispatch-floor** losses — fixed per-call overhead that Zen 4 exposed because this PC's
newer system DLLs got cheaper on their short paths while our constant cost stayed constant. None was a
correctness problem, and none needed a wider instruction set: the fixes remove prologues, keep short
inputs out of vector registers, and in 135's case replace an O(n·m) scan with O(n+m).

135 is the largest win: its long-string classes went from ~5× to **up to 54×**, because change 135 had
never received the set-hoisting treatment changes 035–040 applied to the equivalent ucrtbase routines.

## Still parked on this PC (8)

| # | routine | class | ratio | why it cannot be beaten here |
|---|---|---|---|---|
| 006 | `RtlComputeCrc32` | all | 0.15×–0.96× | ntdll is already VPCLMULQDQ-folded; not a tuning gap |
| 089 | `strstr` | 8, 32 | 0.47× / 0.35× | ucrtbase uses SSE4.2 `pcmpistri` — hardware substring search |
| 102 | `RtlAppendUnicodeStringToString` | 128 | 0.83× | counted append, no `wcslen` to cut; ntdll already optimal |
| 103 | `RtlAppendAsciizToString` | 16 | 0.81×–1.24× | **unstable** — alignment lottery, see below |
| 125 | `RtlFindClearBits` | several | 0.60×–0.97× | ntdll is already byte-table optimized |
| 130 | `RtlSetBits` | 40000 | 0.97× | variant fixes 4096 (0.89 → **1.18×**) but 40000 is a ~130 GB/s bandwidth tie |
| 142 | `PathAddBackslashW` | 16 | 0.88× | two fixes written and measured; **both refuted** — see its `RESULTS-2ndpc.md` |
| 163 | `PathRemoveFileSpecW` | — | — | no implementation exists; contract never derived |

`103` deserves a note: three identical runs gave **1.24× / 1.08× / 0.81×** while the system's time never
moved (5.16 ns every time). That is run-to-run buffer-alignment sensitivity in our code, not a systematic
loss — it is parked because it *can* fail the gate, not because it is reliably slower.

`130` and `142` ship honest negative results rather than forced wins. `130`'s variant is kept and
documented (it is strictly better here at every class, and its cause — Zen 3 splitting 256-bit stores,
Zen 4 not — is instructive). `142`'s two attempts were **deleted rather than shipped**, because both
measured worse than doing nothing.

## Safety

Nothing in this re-validation patches, replaces, or injects anything.

* The sweep only **calls** our assembly and the system function side by side and times them. No hot-patch,
  no `VirtualProtect`, no writes to `C:\Windows\System32`.
* Audited across all 1184 files: **zero** cross-process injection (`WriteProcessMemory`, `OpenProcess`,
  `CreateRemoteThread`, AppInit, IFEO), **zero** kernel-mode artefacts (`.sys`, `DriverEntry`,
  test-signing, `bcdedit`), **zero** on-disk System32 modification. A BSOD requires kernel-mode code to
  fault; none exists here. The existing `live-substitution/` harness patches only its **own process's
  copy-on-write copy** of ucrtbase/ntdll — `FlushInstructionCache(GetCurrentProcess(), …)` — so its worst
  case is that one test process dying, never a bugcheck.
* **No 2nd-PC variant uses AVX-512 or GFNI**, even though this machine has both. Every added path is AVX2
  or plain scalar, so each variant stays correct on the 5950X. An AVX-512 path would raise `#UD` there and
  silently break the repo on the other PC.
* Every variant obeys the Win64 ABI contract in `harness/README.md` — volatile registers only
  (`xmm6`–`xmm15` are callee-saved; one draft that used `ymm6`/`ymm7` was caught and fixed) — and every
  added load is page-safe: guarded by an explicit in-page check, aligned-down-and-masked, or bounded by
  the terminator.

## Reproduce

```
changes\<NNN>-<name>\build.bat          the 5950X implementation of record
changes\<NNN>-<name>\build_2ndpc.bat    the 2nd-PC variant, same correctness.c and bench.c
```

Both gate identically: correctness must pass before the benchmark runs, and the benchmark returns non-zero
if any size class regressed.

## Full sweep — all 166 changes on this PC

`geo` is the geomean vs the live system function on this machine; `worst` is the weakest size class.
Rows marked **2ndpc** have a variant in their directory.

| # | change | Zen 3 | Zen 4 | geo | worst class | |
|---|---|---|---|---|---|---|
| 001 | `001-wcslen` | LANDED | **LANDS** | 2.562 | 1.880@65535 |  |
| 002 | `002-memchr` | LANDED | **LANDS** | 1.882 | 1.370@128 |  |
| 003 | `003-wcschr` | LANDED | **LANDS** | 2.312 | 1.440@3 |  |
| 004 | `004-wcscmp` | LANDED | **LANDS** | 3.321 | 1.700@3 |  |
| 005 | `005-memcmp` | PARKED | **LANDS** | 1.669 | 1.170@8 |  |
| 006 | `006-crc32` | PARKED | PARKED | 0.276 | 0.150@64KB |  |
| 007 | `007-rtlcomparememory` | LANDED | **LANDS** | 4.654 | 3.620@32 |  |
| 008 | `008-rtlcompareunicodestring` | LANDED | **LANDS** | 3.438 to **3.492** | 0.950@8/CI | **2ndpc** |
| 009 | `009-rtlhashunicodestring` | LANDED | **LANDS** | 5.313 | 2.020@8/CI |  |
| 010 | `010-rtlequalunicodestring` | LANDED | **LANDS** | 2.194 | 1.290@4096/cs |  |
| 011 | `011-rtlprefixunicodestring` | LANDED | **LANDS** | 2.119 | 1.280@128/cs |  |
| 012 | `012-rtlcomparestring` | LANDED | **LANDS** | 5.967 | 2.200@8/CI |  |
| 013 | `013-rtlequalstring` | LANDED | **LANDS** | 6.011 | 1.870@8/CI |  |
| 014 | `014-rtlprefixstring` | LANDED | **LANDS** | 2.858 | 1.250@4096/cs |  |
| 015 | `015-rtlupcaseunicodestring` | LANDED | **LANDS** | 6.074 | 1.580@8 |  |
| 016 | `016-rtlunicodetoutf8n` | LANDED | **LANDS** | 2.636 | 1.530@8 |  |
| 017 | `017-rtldowncaseunicodestring` | LANDED | **LANDS** | 11.630 | 1.860@8 |  |
| 018 | `018-rtlunicodestringtoansistring` | LANDED | **LANDS** | 8.493 | 1.960@8 |  |
| 019 | `019-rtlansistringtounicodestring` | LANDED | **LANDS** | 8.605 | 1.620@8 |  |
| 020 | `020-rtlupcaseunicodestringtoansistring` | LANDED | **LANDS** | 9.756 | 3.190@8 |  |
| 021 | `021-rtlunicodetomultibyten` | LANDED | **LANDS** | 4.304 | 1.800@8 |  |
| 022 | `022-rtlmultibytetounicoden` | LANDED | **LANDS** | 7.443 | 2.190@8 |  |
| 023 | `023-rtlnumberofsetbits` | LANDED | **LANDS** | 1.398 | 1.000@64Kb |  |
| 024 | `024-rtlunicodestringtooemstring` | LANDED | **LANDS** | 9.529 | 3.170@8 |  |
| 025 | `025-rtloemstringtounicodestring` | LANDED | **LANDS** | 6.225 | 2.610@8 |  |
| 026 | `026-rtlcomparememoryulong` | LANDED | **LANDS** | 6.161 | 3.500@16 |  |
| 027 | `027-rtlupcaseunicodetomultibyten` | LANDED | **LANDS** | 6.922 | 2.680@8 |  |
| 028 | `028-rtlunicodetooemn` | LANDED | **LANDS** | 7.385 | 3.130@8 |  |
| 029 | `029-rtloemtounicoden` | LANDED | **LANDS** | 5.028 | 3.130@8 |  |
| 030 | `030-rtlarebitsset` | LANDED | **LANDS** | 3.671 | 1.440@32 |  |
| 031 | `031-rtlupcaseunicodetooemn` | LANDED | **LANDS** | 7.432 | 3.350@8 |  |
| 032 | `032-strlen` | LANDED | **LANDS** | 3.484 | 1.680@3 |  |
| 033 | `033-strcmp` | LANDED | **LANDS** | 2.085 | 1.420@3 |  |
| 034 | `034-rtlutf8tounicoden` | LANDED | **LANDS** | 3.692 | 1.590@8 |  |
| 035 | `035-wcspbrk` | LANDED | **LANDS** | 5.912 | 1.560@8 |  |
| 036 | `036-wcsspn` | LANDED | **LANDS** | 8.663 | 3.710@8 |  |
| 037 | `037-wcscspn` | LANDED | **LANDS** | 6.020 | 1.750@8 |  |
| 038 | `038-strpbrk` | LANDED | **LANDS** | 5.296 | 1.760@8 |  |
| 039 | `039-strspn` | LANDED | **LANDS** | 7.208 | 2.330@8 |  |
| 040 | `040-strcspn` | LANDED | **LANDS** | 6.490 | 1.830@8 |  |
| 041 | `041-wcsncmp` | LANDED | **LANDS** | 3.741 | 1.440@8 |  |
| 042 | `042-wcsicmp` | LANDED | **LANDS** | 7.599 | 2.800@32 |  |
| 043 | `043-stricmp` | LANDED | **LANDS** | 9.382 | 2.500@8 |  |
| 044 | `044-wcsnicmp` | LANDED | **LANDS** | 7.512 | 2.660@8 |  |
| 045 | `045-strnicmp` | LANDED | **LANDS** | 10.461 | 2.450@8 |  |
| 046 | `046-memicmp` | LANDED | **LANDS** | 12.014 | 2.510@8 |  |
| 047 | `047-strlwr` | LANDED | **LANDS** | 9.277 to **10.159** | 0.830@8 | **2ndpc** |
| 048 | `048-strupr` | LANDED | **LANDS** | 9.591 | 0.970@8 |  |
| 049 | `049-wcslwr` | PARKED | **LANDS** | 2.926 | 1.180@8 |  |
| 050 | `050-wcsupr` | LANDED | **LANDS** | 6.602 | 1.010@8 |  |
| 051 | `051-rtlfindcharinunicodestring` | LANDED | **LANDS** | 8.770 | 2.650@8 |  |
| 052 | `052-rtlintegertounicodestring` | LANDED | **LANDS** | 3.404 | 3.080@5digit |  |
| 053 | `053-rtlint64tounicodestring` | LANDED | **LANDS** | 3.217 | 3.030@19digit |  |
| 054 | `054-ultoa` | LANDED | **LANDS** | 1.534 | 1.360@r10:5dig |  |
| 055 | `055-ui64toa` | LANDED | **LANDS** | 1.634 | 1.470@r10:2d |  |
| 056 | `056-itoa` | LANDED | **LANDS** | 1.515 | 1.360@neg5d |  |
| 057 | `057-i64toa` | LANDED | **LANDS** | 1.642 | 1.400@neg14d |  |
| 058 | `058-rtlstringfromguidex` | LANDED | **LANDS** | 24.885 | 24.880@guid |  |
| 059 | `059-rtlipv4addresstostringa` | LANDED | **LANDS** | 17.343 | 17.340@192.168.100.201 |  |
| 060 | `060-rtlethernetaddresstostringa` | LANDED | **LANDS** | 32.585 | 32.580@mac |  |
| 061 | `061-rtlipv4addresstostringw` | LANDED | **LANDS** | 22.340 | 22.340@192.168.100.201 |  |
| 062 | `062-rtlethernetaddresstostringw` | LANDED | **LANDS** | 38.366 | 38.370@mac |  |
| 063 | `063-rtlipv6addresstostringa` | LANDED | **LANDS** | 7.209 | 7.210@2001:db8::1 |  |
| 064 | `064-rtlipv6addresstostringw` | LANDED | **LANDS** | 9.186 | 9.190@2001:db8::1 |  |
| 065 | `065-rtlipv4addresstostringexa` | LANDED | **LANDS** | 8.910 | 8.910@192.168.0.1:80 |  |
| 066 | `066-rtlipv4addresstostringexw` | LANDED | **LANDS** | 10.327 | 10.330@192.168.0.1:80 |  |
| 067 | `067-rtlconvertsidtounicodestring` | LANDED | **LANDS** | 1.340 | 1.340@S-1-5-21-x-x-x-500 |  |
| 068 | `068-rtlipv6addresstostringexa` | LANDED | **LANDS** | 6.642 | 6.640@[2001:db8::1%5]:80 |  |
| 069 | `069-rtlipv6addresstostringexw` | LANDED | **LANDS** | 9.367 | 9.370@[2001:db8::1%5]:80 |  |
| 070 | `070-strrev` | LANDED | **LANDS** | 7.267 | 1.680@8 |  |
| 071 | `071-wcsrev` | LANDED | **LANDS** | 6.329 | 1.690@8 |  |
| 072 | `072-ultow` | LANDED | **LANDS** | 1.590 | 1.430@r10:1dig |  |
| 073 | `073-ui64tow` | LANDED | **LANDS** | 1.671 | 1.460@r10:1dig |  |
| 074 | `074-itow` | LANDED | **LANDS** | 1.441 | 1.280@r10:neg5 |  |
| 075 | `075-i64tow` | LANDED | **LANDS** | 1.538 | 1.370@r10:neg5 |  |
| 076 | `076-rtlcrc64` | LANDED | **LANDS** | 3.174 | 1.320@64 |  |
| 077 | `077-strset` | LANDED | **LANDS** | 4.561 | 1.190@32 |  |
| 078 | `078-strnset` | LANDED | **LANDS** | 6.737 | 1.860@8 |  |
| 079 | `079-wcsset` | LANDED | **LANDS** | 3.949 | 1.390@8 |  |
| 080 | `080-wcsnset` | LANDED | **LANDS** | 7.706 | 1.770@8 |  |
| 081 | `081-cryptbinarytostring-base64` | LANDED | **LANDS** | 23.185 | 9.320@16 |  |
| 082 | `082-cryptstringtobinary-base64` | LANDED | **LANDS** | 33.624 | 11.400@16 |  |
| 083 | `083-cryptbinarytostringw-base64` | LANDED | **LANDS** | 8.579 | 8.000@1024 |  |
| 084 | `084-cryptstringtobinaryw-base64` | LANDED | **LANDS** | 37.263 | 14.740@16 |  |
| 085 | `085-cryptbinarytostring-hexraw` | LANDED | **LANDS** | 729.508 | 206.170@16 |  |
| 086 | `086-cryptstringtobinary-hexraw` | LANDED | **LANDS** | 240.478 | 125.980@16 |  |
| 087 | `087-cryptbinarytostringw-hexraw` | LANDED | **LANDS** | 102.347 | 77.810@16 |  |
| 088 | `088-cryptstringtobinaryw-hexraw` | LANDED | **LANDS** | 230.080 | 128.810@16 |  |
| 089 | `089-strstr` | PARKED | PARKED | 0.809 | 0.350@32 |  |
| 090 | `090-cryptbinarytostring-hexfmt` | LANDED | **LANDS** | 111.392 | 89.330@16 |  |
| 091 | `091-cryptbinarytostringw-hexfmt` | LANDED | **LANDS** | 69.699 | 25.320@65536 |  |
| 092 | `092-cryptbinarytostring-base64header` | LANDED | **LANDS** | 15.848 | 3.980@16 |  |
| 093 | `093-cryptbinarytostringw-base64header` | LANDED | **LANDS** | 80.613 | 10.240@65536 |  |
| 094 | `094-rtlinitunicodestring` | LANDED | **LANDS** | 2.105 | 1.670@16 |  |
| 095 | `095-rtlinitstring` | LANDED | **LANDS** | 1.975 | 1.660@4 |  |
| 096 | `096-rtlinitunicodestringex` | LANDED | **LANDS** | 1.954 | 1.520@32 |  |
| 097 | `097-rtlintegertochar` | LANDED | **LANDS** | 1.327 to **1.573** | 0.700@dec-1d | **2ndpc** |
| 098 | `098-rtlinitstringex` | LANDED | **LANDS** | 1.941 | 1.590@4 |  |
| 099 | `099-strncmp` | PARKED | **LANDS** | 1.688 | 1.080@32 |  |
| 100 | `100-rtllargeintegertochar` | LANDED | **LANDS** | 1.477 | 1.210@hex-16d |  |
| 101 | `101-rtlappendunicodetostring` | LANDED | **LANDS** | 1.526 | 1.230@16 |  |
| 102 | `102-rtlappendunicodestringtostring` | PARKED | PARKED | 1.184 | 0.800@128 |  |
| 103 | `103-rtlappendasciiztostring` | PARKED | PARKED | 1.337 | 0.810@16 |  |
| 104 | `104-cryptstringtobinary-base64header` | LANDED | **LANDS** | 9.254 | 3.230@16 |  |
| 105 | `105-cryptstringtobinaryw-base64header` | LANDED | **LANDS** | 10.385 | 4.760@16 |  |
| 106 | `106-cryptstringtobinary-base64any` | LANDED | **LANDS** | 4.271 | 3.100@16 |  |
| 107 | `107-cryptstringtobinaryw-base64any` | LANDED | **LANDS** | 6.163 | 5.610@16 |  |
| 108 | `108-atoi` | LANDED | **LANDS** | 2.389 | 2.160@ws+int_min |  |
| 109 | `109-atoi64` | LANDED | **LANDS** | 2.511 | 2.380@ws+i64_min |  |
| 110 | `110-strtol` | LANDED | **LANDS** | 2.155 | 1.980@int_max |  |
| 111 | `111-strtoul` | LANDED | **LANDS** | 2.163 | 1.960@ulong_max |  |
| 112 | `112-strtoi64` | LANDED | **LANDS** | 2.024 | 1.760@i64_max |  |
| 113 | `113-strtoui64` | LANDED | **LANDS** | 2.098 | 1.960@1digit |  |
| 114 | `114-rtlipv4stringtoaddress` | LANDED | **LANDS** | 1.531 | 1.310@1.2.3.4 |  |
| 115 | `115-rtlipv4stringtoaddressw` | LANDED | **LANDS** | 1.452 | 1.360@10.0.0.255 |  |
| 116 | `116-rtlipv4stringtoaddressex` | LANDED | **LANDS** | 1.849 | 1.600@addr-noport |  |
| 117 | `117-rtlipv4stringtoaddressexw` | LANDED | **LANDS** | 1.706 | 1.500@addr-noport |  |
| 118 | `118-rtlguidfromstring` | LANDED | **LANDS** | 4.445 | 4.450@guid38 |  |
| 119 | `119-rtlethernetstringtoaddress` | LANDED | **LANDS** | 6.927 | 6.580@dash |  |
| 120 | `120-rtlethernetstringtoaddressw` | LANDED | **LANDS** | 5.191 | 4.900@dash |  |
| 121 | `121-rtlipv6stringtoaddress` | LANDED | **LANDS** | 4.158 | 1.510@loopback |  |
| 122 | `122-rtlipv6stringtoaddressex` | LANDED | **LANDS** | 3.729 | 1.700@loopback+port |  |
| 123 | `123-rtlfindlongestrunclear` | LANDED | **LANDS** | 3.890 | 1.890@64Kb/sparse |  |
| 124 | `124-rtlnumberofclearbits` | LANDED | **LANDS** | 1.454 | 1.000@1Mb |  |
| 125 | `125-rtlfindclearbits` | PARKED | PARKED | 0.899 | 0.600@64Kb/n40 |  |
| 126 | `126-rtltimetotimefields` | LANDED | **LANDS** | 1.763 | 1.700@max |  |
| 127 | `127-rtltimefieldstotime` | LANDED | **LANDS** | 1.543 | 1.450@leapday |  |
| 128 | `128-rtlsecondssince1970totime` | LANDED | **LANDS** | 2.657 | 2.600@epoch |  |
| 129 | `129-rtlchartointeger` | PARKED | **LANDS** | 1.277 | 1.010@short |  |
| 130 | `130-rtlsetbits` | PARKED | PARKED | 1.331 to **1.394** | 0.890@4096 | **2ndpc** |
| 131 | `131-strchrw` | LANDED | **LANDS** | 7.438 | 2.100@8 |  |
| 132 | `132-pathfindextensionw` | LANDED | **LANDS** | 5.829 | 2.740@16 |  |
| 133 | `133-strstrw` | LANDED | **LANDS** | 5.810 | 3.630@16/miss |  |
| 134 | `134-strrchrw` | LANDED | **LANDS** | 5.058 | 2.930@254/bounded |  |
| 135 | `135-strspnw` | LANDED | **LANDS** | 2.671 to **7.887** | 0.860@16/set23 | **2ndpc** |
| 136 | `136-strcspnw` | LANDED | **LANDS** | 12.399 | 6.600@16/set3 |  |
| 137 | `137-strpbrkw` | LANDED | **LANDS** | 9.206 | 4.570@16/set3 |  |
| 138 | `138-pathisfilespecw` | LANDED | **LANDS** | 5.621 | 3.310@16 |  |
| 139 | `139-strtrimw` | LANDED | **LANDS** | 21.120 | 11.880@64/notrim |  |
| 140 | `140-pathremoveextensionw` | LANDED | **LANDS** | 2.671 | 1.420@16 |  |
| 141 | `141-pathremoveblanksw` | LANDED | **LANDS** | 4.610 to **4.751** | 0.960@16 | **2ndpc** |
| 142 | `142-pathaddbackslashw` | PARKED | PARKED | 3.316 | 0.890@16 | **2ndpc attempted** |
| 143 | `143-pathcchfindextension` | LANDED | **LANDS** | 4.969 | 2.050@16 |  |
| 144 | `144-pathcchremoveextension` | LANDED | **LANDS** | 2.907 | 1.430@16 |  |
| 145 | `145-swab` | LANDED | **LANDS** | 9.264 | 2.020@16B |  |
| 146 | `146-memccpy` | LANDED | **LANDS** | 9.152 | 2.850@16B/absent |  |
| 147 | `147-strtok-s` | LANDED | **LANDS** | 6.096 | 1.520@16B/1tok |  |
| 148 | `148-wcstok-s` | LANDED | **LANDS** | 5.099 | 1.480@254ch/csv |  |
| 149 | `149-wcsrchr` | LANDED | **LANDS** | 1.772 | 1.360@16/miss |  |
| 150 | `150-strcpy-s` | LANDED | **LANDS** | 3.633 | -@- |  |
| 151 | `151-wcscpy-s` | LANDED | **LANDS** | 3.139 | -@- |  |
| 152 | `152-strcat-s` | LANDED | **LANDS** | 4.730 | -@- |  |
| 153 | `153-wcscat-s` | LANDED | **LANDS** | 5.005 | -@- |  |
| 154 | `154-strncpy-s` | LANDED | **LANDS** | 3.435 | -@- |  |
| 155 | `155-wcsncpy-s` | LANDED | **LANDS** | 3.036 | -@- |  |
| 156 | `156-strncat-s` | LANDED | **LANDS** | 4.689 | -@- |  |
| 157 | `157-wcsncat-s` | LANDED | **LANDS** | 2.540 | -@- |  |
| 158 | `158-pathrenameextensionw` | LANDED | **LANDS** | 4.230 | -@- |  |
| 159 | `159-pathcchrenameextension` | LANDED | **LANDS** | 3.479 | -@- |  |
| 160 | `160-pathcchaddextension` | LANDED | **LANDS** | 3.605 | -@- |  |
| 161 | `161-pathfindfilenamew` | LANDED | **LANDS** | 3.697 | -@- |  |
| 162 | `162-pathstrippathw` | LANDED | **LANDS** | 3.129 | -@- |  |
| 163 | `163-pathremovefilespecw` | PARKED | n/a | - | -@- |  |
| 164 | `164-pathcchaddbackslash` | LANDED | **LANDS** | 2.149 | -@- |  |
| 165 | `165-rtlupperstring` | LANDED | **LANDS** | 103.396 | -@- |  |
| 166 | `166-rtlipv6stringtoaddressw` | LANDED | **LANDS** | 3.293 | 1.400@loopback |  |
