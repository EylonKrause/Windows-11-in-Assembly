# Sweep results — Intel Core i9-11900H (Tiger Lake-H, Willow Cove) — bench #3

Machine capture: [`docs/PLATFORM-i9-11900H.md`](docs/PLATFORM-i9-11900H.md). Source sweep: `results_2026-09-20_100933.tsv` (`revalidation/` is git-ignored; this file is the part worth keeping).

Every row was rebuilt from source on this machine and re-checked against the **live** export resolved through `GetProcAddress`, so this is not a replay of a stored number — it is an independent re-proof of each contract against the Windows binaries this PC actually runs.

## Headline

| | count |
|---|---:|
| changes swept | **288** |
| clean win here (no size class regressed) | **273** |
| correctness / build failures | **0** |
| proven elsewhere, regresses a size class here | **8** |
| already PARKED (expected to lose somewhere) | **5** |

### Correctness verdicts re-derived from the logs

`revalidate.ps1` classified these from its own output, and its rule matched the word “mismatch” inside the phrase “**0** mismatches” — the phrase a PASSING harness prints. Each change below reports its verdict only in that form and never prints a bare `PASS`, so the bare-word rule condemned it. The classifier is fixed; these are re-derived here from the same logs so the correction does not require an 80-minute re-sweep to take effect.

Each one's log also contains a **complete benchmark table**, which by itself proves `correctness.exe` exited 0 — `build.bat` gates on it and refuses to benchmark otherwise.

| change | recorded | actual |
|---|---|---|
| `167-pathcommonprefixw` | FAIL | **PASS** |
| `177-pathisprefixw` | FAIL | **PASS** |
| `248-urlunescapea` | FAIL | **PASS** |
| `249-urlhasha` | FAIL | **PASS** |
| `250-rtlipv6stringtoaddressexw` | FAIL | **PASS** |
| `251-pathissamerootw` | FAIL | **PASS** |

## Microarchitecture divergence — proven elsewhere, regresses here

These are the rows the multi-machine exercise exists to find. **The parent change is not edited.** Each gets a forked variant (`tools/new-variant.py`) so both microarchitectures keep an attributable result.

| change | geomean here | worst class | regressed classes | geomean on its own bench |
|---|---:|---|---|---:|
| `023-rtlnumberofsetbits` | 1.213x | 0.740x @ 1Mb | 8Kb=0.87x,64Kb=0.76x,1Mb=0.74x | 1.32x |
| `124-rtlnumberofclearbits` | 1.256x | 0.810x @ 1Mb | 8Kb=0.93x,64Kb=0.83x,1Mb=0.81x | 1.33x |
| `183-wcsset-s` | 1.739x | 0.780x @ 8 | 8=0.78x | 1.99x |
| `003-wcschr` | 2.011x | 0.870x @ 3 | 3=0.87x | 2.19x |
| `185-wcsnset-s` | 2.297x | 0.820x @ 8 | 8=0.82x | 2.52x |
| `182-strset-s` | 2.460x | 0.830x @ 8 | 8=0.83x | 3.60x |
| `184-strnset-s` | 2.628x | 0.860x @ 8 | 8=0.86x | 2.99x |
| `008-rtlcompareunicodestring` | 3.205x | 0.940x @ 8/CI | 8/CI=0.94x | 3.97x |

## Already parked — losing a class here is the documented behaviour

| change | geomean here | worst class |
|---|---:|---|
| `006-crc32` | 0.255x | 0.140x @ 8KB |
| `089-strstr` | 0.719x | 0.380x @ 8 |
| `102-rtlappendunicodestringtostring` | 1.098x | 0.750x @ 128 |
| `125-rtlfindclearbits` | 0.905x | 0.580x @ 64Kb/n40 |
| `130-rtlsetbits` | 1.153x | 0.550x @ 40000 |

## Where this machine differs most from the bench that proved the change

Ratio of the geomean measured here to the geomean in the change's own `RESULTS.md`. Above 1.00 means this machine likes our code *more* than the proving bench did. Both numbers are against the live system function, so a large move is a statement about the two microarchitectures — or about Windows having serviced the function in between.

| change | here | its own bench | here / there |
|---|---:|---:|---:|
| `135-strspnw` | 3.39x | 5.87x | **0.58** |
| `017-rtldowncaseunicodestring` | 6.65x | 11.15x | **0.60** |
| `215-strpbrka` | 102.48x | 167.54x | **0.61** |
| `213-strrchra` | 94.47x | 149.12x | **0.63** |
| `217-pathfindextensiona` | 28.94x | 45.48x | **0.64** |
| `259-rtlarebitsset` | 2.31x | 3.63x | **0.64** |
| `216-strspna` | 145.75x | 227.43x | **0.64** |
| `002-memchr` | 1.49x | 2.28x | **0.65** |
| `076-rtlcrc64` | 2.18x | 3.29x | **0.66** |
| `030-rtlarebitsset` | 2.10x | 3.17x | **0.66** |
| `139-strtrimw` | 11.47x | 17.24x | **0.67** |
| `235-pathisfilespeca` | 37.14x | 55.14x | **0.67** |
| `252-rtlfindunicodesubstring` | 15.70x | 23.07x | **0.68** |
| `182-strset-s` | 2.46x | 3.60x | **0.68** |
| `223-pathundecoratea` | 18.72x | 27.05x | **0.69** |
| … | | | |
| `105-cryptstringtobinaryw-base64header` | 8.26x | 5.90x | **1.40** |
| `025-rtloemstringtounicodestring` | 7.71x | 5.50x | **1.40** |
| `107-cryptstringtobinaryw-base64any` | 5.44x | 3.80x | **1.43** |
| `232-pathremovebackslasha` | 4.44x | 2.99x | **1.49** |
| `033-strcmp` | 2.04x | 1.31x | **1.56** |
| `128-rtlsecondssince1970totime` | 3.16x | 2.00x | **1.58** |
| `052-rtlintegertounicodestring` | 4.72x | 2.94x | **1.61** |
| `078-strnset` | 9.20x | 5.72x | **1.61** |
| `038-strpbrk` | 5.79x | 3.53x | **1.64** |
| `134-strrchrw` | 7.84x | 4.75x | **1.65** |
| `039-strspn` | 8.23x | 4.82x | **1.71** |
| `131-strchrw` | 6.33x | 3.67x | **1.72** |
| `147-strtok-s` | 9.51x | 5.43x | **1.75** |
| `040-strcspn` | 6.68x | 3.31x | **2.02** |
| `092-cryptbinarytostring-base64header` | 18.13x | 6.10x | **2.97** |

Rows compared: 270. Rows whose own RESULTS.md geomean could not be parsed are omitted rather than guessed.

## Full table

| change | status | correctness | geomean | worst class | regressed |
|---|---|---|---:|---|---|
| `001-wcslen` | LANDS | PASS | 2.384 | 1.230 @ 8191 | - |
| `002-memchr` | LANDS | PASS | 1.490 | 1.000 @ 128 | - |
| `003-wcschr` | REGRESSED | PASS | 2.011 | 0.870 @ 3 | 3=0.87x |
| `004-wcscmp` | LANDS | PASS | 3.432 | 2.080 @ 3 | - |
| `005-memcmp` | LANDS | PASS | 1.598 | 1.170 @ 8 | - |
| `006-crc32` | REGRESSED | PASS | 0.255 | 0.140 @ 8KB | 16=0.94x,64=0.34x,256=0.39x,1KB=0.20x,8KB=0.14x,64KB=0.14x,1MB=0.14x |
| `007-rtlcomparememory` | LANDS | PASS | 4.114 | 1.740 @ 1MB | - |
| `008-rtlcompareunicodestring` | REGRESSED | PASS | 3.205 | 0.940 @ 8/CI | 8/CI=0.94x |
| `009-rtlhashunicodestring` | LANDS | PASS | 3.463 | 1.810 @ 8/CI | - |
| `010-rtlequalunicodestring` | LANDS | PASS | 1.924 | 1.360 @ 512/cs | - |
| `011-rtlprefixunicodestring` | LANDS | PASS | 1.842 | 1.150 @ 512/cs | - |
| `012-rtlcomparestring` | LANDS | PASS | 5.025 | 1.710 @ 8/CI | - |
| `013-rtlequalstring` | LANDS | PASS | 5.319 | 2.000 @ 8/CI | - |
| `014-rtlprefixstring` | LANDS | PASS | 2.967 | 1.430 @ 32000/cs | - |
| `015-rtlupcaseunicodestring` | LANDS | PASS | 6.750 | 2.000 @ 8 | - |
| `016-rtlunicodetoutf8n` | LANDS | PASS | 2.169 | - @ - | - |
| `017-rtldowncaseunicodestring` | LANDS | PASS | 6.652 | 1.780 @ 8 | - |
| `018-rtlunicodestringtoansistring` | LANDS | PASS | 9.064 | 5.830 @ 8 | - |
| `019-rtlansistringtounicodestring` | LANDS | PASS | 9.341 | 3.970 @ 8 | - |
| `020-rtlupcaseunicodestringtoansistring` | LANDS | PASS | 9.002 | 6.840 @ 8 | - |
| `021-rtlunicodetomultibyten` | LANDS | PASS | 4.870 | 3.430 @ 8 | - |
| `022-rtlmultibytetounicoden` | LANDS | PASS | 5.587 | 3.980 @ 8 | - |
| `023-rtlnumberofsetbits` | REGRESSED | PASS | 1.213 | 0.740 @ 1Mb | 8Kb=0.87x,64Kb=0.76x,1Mb=0.74x |
| `024-rtlunicodestringtooemstring` | LANDS | PASS | 8.111 | 6.400 @ 8 | - |
| `025-rtloemstringtounicodestring` | LANDS | PASS | 7.705 | 5.130 @ 8 | - |
| `026-rtlcomparememoryulong` | LANDS | PASS | 7.046 | 4.750 @ 16 | - |
| `027-rtlupcaseunicodetomultibyten` | LANDS | PASS | 3.324 | - @ - | - |
| `028-rtlunicodetooemn` | LANDS | PASS | 5.800 | 5.120 @ 8 | - |
| `029-rtloemtounicoden` | LANDS | PASS | 5.751 | 5.180 @ 512 | - |
| `030-rtlarebitsset` | LANDS | PASS | 2.102 | 1.160 @ 32 | - |
| `031-rtlupcaseunicodetooemn` | LANDS | PASS | 3.337 | - @ - | - |
| `032-strlen` | LANDS | PASS | 3.645 | 1.390 @ 3 | - |
| `033-strcmp` | LANDS | PASS | 2.045 | 1.250 @ 3 | - |
| `034-rtlutf8tounicoden` | LANDS | PASS | 3.333 | - @ - | - |
| `035-wcspbrk` | LANDS | PASS | 4.576 | 1.260 @ 8 | - |
| `036-wcsspn` | LANDS | PASS | 7.445 | 3.240 @ 8 | - |
| `037-wcscspn` | LANDS | PASS | 5.560 | 1.770 @ 8 | - |
| `038-strpbrk` | LANDS | PASS | 5.789 | 1.910 @ 8 | - |
| `039-strspn` | LANDS | PASS | 8.235 | 2.960 @ 8 | - |
| `040-strcspn` | LANDS | PASS | 6.679 | 2.320 @ 8 | - |
| `041-wcsncmp` | LANDS | PASS | 3.936 | 1.680 @ 8 | - |
| `042-wcsicmp` | LANDS | PASS | 7.471 | 3.710 @ 8 | - |
| `043-stricmp` | LANDS | PASS | 8.525 | 2.770 @ 8 | - |
| `044-wcsnicmp` | LANDS | PASS | 6.588 | 3.080 @ 8 | - |
| `045-strnicmp` | LANDS | PASS | 9.811 | 2.580 @ 8 | - |
| `046-memicmp` | LANDS | PASS | 10.912 | 2.570 @ 8 | - |
| `047-strlwr` | LANDS | PASS | 8.346 | 1.150 @ 8 | - |
| `048-strupr` | LANDS | PASS | 7.763 | 1.360 @ 8 | - |
| `049-wcslwr` | LANDS | PASS | 3.320 | 1.280 @ 8 | - |
| `050-wcsupr` | LANDS | PASS | 4.809 | 1.350 @ 8 | - |
| `051-rtlfindcharinunicodestring` | LANDS | PASS | 8.568 | 3.080 @ 8 | - |
| `052-rtlintegertounicodestring` | LANDS | PASS | 4.724 | 3.890 @ 10digit | - |
| `053-rtlint64tounicodestring` | LANDS | PASS | 4.069 | 3.330 @ 19digit | - |
| `054-ultoa` | LANDS | PASS | 1.630 | 1.330 @ r10:5dig | - |
| `055-ui64toa` | LANDS | PASS | 1.937 | 1.500 @ r10:2d | - |
| `056-itoa` | LANDS | PASS | 1.655 | 1.320 @ neg5d | - |
| `057-i64toa` | LANDS | PASS | 2.062 | 1.710 @ neg2d | - |
| `058-rtlstringfromguidex` | LANDS | PASS | 51.973 | 51.970 @ guid | - |
| `059-rtlipv4addresstostringa` | LANDS | PASS | 17.971 | 17.970 @ 192.168.100.201 | - |
| `060-rtlethernetaddresstostringa` | LANDS | PASS | 42.623 | 42.620 @ mac | - |
| `061-rtlipv4addresstostringw` | LANDS | PASS | 56.056 | 56.060 @ 192.168.100.201 | - |
| `062-rtlethernetaddresstostringw` | LANDS | PASS | 70.234 | 70.230 @ mac | - |
| `063-rtlipv6addresstostringa` | LANDS | PASS | 6.992 | 6.990 @ 2001:db8::1 | - |
| `064-rtlipv6addresstostringw` | LANDS | PASS | 13.588 | 13.590 @ 2001:db8::1 | - |
| `065-rtlipv4addresstostringexa` | LANDS | PASS | 11.654 | 11.650 @ 192.168.0.1:80 | - |
| `066-rtlipv4addresstostringexw` | LANDS | PASS | 19.459 | 19.460 @ 192.168.0.1:80 | - |
| `067-rtlconvertsidtounicodestring` | LANDS | PASS | 3.133 | - @ - | - |
| `068-rtlipv6addresstostringexa` | LANDS | PASS | 7.878 | 7.880 @ [2001:db8::1%5]:80 | - |
| `069-rtlipv6addresstostringexw` | LANDS | PASS | 14.891 | 14.890 @ [2001:db8::1%5]:80 | - |
| `070-strrev` | LANDS | PASS | 8.231 | 1.980 @ 8 | - |
| `071-wcsrev` | LANDS | PASS | 5.349 | 1.620 @ 8 | - |
| `072-ultow` | LANDS | PASS | 1.709 | 1.370 @ r10:5dig | - |
| `073-ui64tow` | LANDS | PASS | 1.982 | 1.400 @ r10:1dig | - |
| `074-itow` | LANDS | PASS | 1.508 | 1.300 @ r10:neg5 | - |
| `075-i64tow` | LANDS | PASS | 1.971 | 1.600 @ r10:neg5 | - |
| `076-rtlcrc64` | LANDS | PASS | 2.179 | 1.150 @ 64 | - |
| `077-strset` | LANDS | PASS | 5.352 | 1.140 @ 32 | - |
| `078-strnset` | LANDS | PASS | 9.200 | 1.750 @ 8 | - |
| `079-wcsset` | LANDS | PASS | 4.447 | 1.430 @ 8 | - |
| `080-wcsnset` | LANDS | PASS | 5.594 | 1.900 @ 8 | - |
| `081-cryptbinarytostring-base64` | LANDS | PASS | 25.671 | 10.790 @ 16 | - |
| `082-cryptstringtobinary-base64` | LANDS | PASS | 25.653 | 9.030 @ 16 | - |
| `083-cryptbinarytostringw-base64` | LANDS | PASS | 9.807 | 8.840 @ 64 | - |
| `084-cryptstringtobinaryw-base64` | LANDS | PASS | 29.376 | 13.260 @ 16 | - |
| `085-cryptbinarytostring-hexraw` | LANDS | PASS | 819.149 | 268.170 @ 16 | - |
| `086-cryptstringtobinary-hexraw` | LANDS | PASS | 198.861 | 116.170 @ 16 | - |
| `087-cryptbinarytostringw-hexraw` | LANDS | PASS | 127.371 | 102.380 @ 64 | - |
| `088-cryptstringtobinaryw-hexraw` | LANDS | PASS | 197.325 | 115.720 @ 16 | - |
| `089-strstr` | REGRESSED | PASS | 0.719 | 0.380 @ 8 | 8=0.38x,32=0.56x,128=0.66x,512=0.68x |
| `090-cryptbinarytostring-hexfmt` | LANDS | PASS | 150.091 | 117.390 @ 16 | - |
| `091-cryptbinarytostringw-hexfmt` | LANDS | PASS | 102.828 | 54.140 @ 65536 | - |
| `092-cryptbinarytostring-base64header` | LANDS | PASS | 18.126 | 4.990 @ 16 | - |
| `093-cryptbinarytostringw-base64header` | LANDS | PASS | 62.733 | 11.490 @ 65536 | - |
| `094-rtlinitunicodestring` | LANDS | PASS | 1.941 | 1.290 @ 16 | - |
| `095-rtlinitstring` | LANDS | PASS | 1.824 | 1.450 @ 4 | - |
| `096-rtlinitunicodestringex` | LANDS | PASS | 1.949 | 1.470 @ 16 | - |
| `097-rtlintegertochar` | LANDS | PASS | 1.425 | 1.250 @ dec-1d | - |
| `098-rtlinitstringex` | LANDS | PASS | 1.866 | 1.400 @ 32 | - |
| `099-strncmp` | LANDS | PASS | 1.800 | 1.180 @ 32 | - |
| `100-rtllargeintegertochar` | LANDS | PASS | 1.505 | 1.200 @ bin-64d | - |
| `101-rtlappendunicodetostring` | LANDS | PASS | 1.442 | 0.980 @ 128 | - |
| `102-rtlappendunicodestringtostring` | REGRESSED | PASS | 1.098 | 0.750 @ 128 | 4=0.94x,128=0.75x |
| `103-rtlappendasciiztostring` | LANDS | PASS | 1.324 | 1.110 @ 32 | - |
| `104-cryptstringtobinary-base64header` | LANDS | PASS | 6.456 | 2.330 @ 16 | - |
| `105-cryptstringtobinaryw-base64header` | LANDS | PASS | 8.257 | 6.160 @ 64 | - |
| `106-cryptstringtobinary-base64any` | LANDS | PASS | 4.880 | 3.000 @ 16 | - |
| `107-cryptstringtobinaryw-base64any` | LANDS | PASS | 5.440 | 4.560 @ 16 | - |
| `108-atoi` | LANDS | PASS | 2.372 | 2.050 @ leadzeros31 | - |
| `109-atoi64` | LANDS | PASS | 2.490 | 2.080 @ leadzeros32 | - |
| `110-strtol` | LANDS | PASS | 2.269 | 2.040 @ 5digit | - |
| `111-strtoul` | LANDS | PASS | 2.249 | 2.060 @ octal6 | - |
| `112-strtoi64` | LANDS | PASS | 1.943 | 1.800 @ i64_max | - |
| `113-strtoui64` | LANDS | PASS | 1.957 | 1.790 @ octal6 | - |
| `114-rtlipv4stringtoaddress` | LANDS | PASS | 1.197 | 1.090 @ 192.168.1.100 | - |
| `115-rtlipv4stringtoaddressw` | LANDS | PASS | 1.460 | 1.340 @ 127.1(short) | - |
| `116-rtlipv4stringtoaddressex` | LANDS | PASS | 1.498 | 1.370 @ addr:65535 | - |
| `117-rtlipv4stringtoaddressexw` | LANDS | PASS | 1.727 | 1.510 @ addr-noport | - |
| `118-rtlguidfromstring` | LANDS | PASS | 6.277 | 6.280 @ guid38 | - |
| `119-rtlethernetstringtoaddress` | LANDS | PASS | 5.005 | 4.960 @ colon | - |
| `120-rtlethernetstringtoaddressw` | LANDS | PASS | 4.630 | 4.520 @ colon | - |
| `121-rtlipv6stringtoaddress` | LANDS | PASS | 3.962 | 1.660 @ loopback | - |
| `122-rtlipv6stringtoaddressex` | LANDS | PASS | 3.814 | 1.790 @ loopback+port | - |
| `123-rtlfindlongestrunclear` | LANDS | PASS | 4.464 | 1.970 @ 256/sparse | - |
| `124-rtlnumberofclearbits` | REGRESSED | PASS | 1.256 | 0.810 @ 1Mb | 8Kb=0.93x,64Kb=0.83x,1Mb=0.81x |
| `125-rtlfindclearbits` | REGRESSED | PASS | 0.905 | 0.580 @ 64Kb/n40 | 8Kb/n20=0.89x,64Kb/n16@8000=0.94x,64Kb/n40=0.58x |
| `126-rtltimetotimefields` | LANDS | PASS | 1.986 | 1.900 @ max | - |
| `127-rtltimefieldstotime` | LANDS | PASS | 1.831 | 1.790 @ leapday | - |
| `128-rtlsecondssince1970totime` | LANDS | PASS | 3.161 | 2.910 @ 2023 | - |
| `129-rtlchartointeger` | LANDS | PASS | 1.249 | 1.320 @ empty | - |
| `130-rtlsetbits` | REGRESSED | PASS | 1.153 | 0.550 @ 40000 | 40000=0.55x,262144=0.86x |
| `131-strchrw` | LANDS | PASS | 6.329 | 2.450 @ 8 | - |
| `132-pathfindextensionw` | LANDS | PASS | 5.558 | 3.120 @ 16 | - |
| `133-strstrw` | LANDS | PASS | 7.789 | 4.040 @ 16/miss | - |
| `134-strrchrw` | LANDS | PASS | 7.836 | 4.510 @ 254/bounded | - |
| `135-strspnw` | LANDS | PASS | 3.387 | 1.890 @ 254/set3-stop0 | - |
| `136-strcspnw` | LANDS | PASS | 9.251 | 6.570 @ 16/set3 | - |
| `137-strpbrkw` | LANDS | PASS | 9.148 | 5.500 @ 16/set3 | - |
| `138-pathisfilespecw` | LANDS | PASS | 6.843 | 3.630 @ 16 | - |
| `139-strtrimw` | LANDS | PASS | 11.469 | 4.440 @ 254/trim8+4 | - |
| `140-pathremoveextensionw` | LANDS | PASS | 3.334 | 1.950 @ 16 | - |
| `141-pathremoveblanksw` | LANDS | PASS | 3.524 | 1.710 @ 16 | - |
| `142-pathaddbackslashw` | LANDS | PASS | 5.462 | 2.200 @ 16 | - |
| `143-pathcchfindextension` | LANDS | PASS | 4.281 | 2.090 @ 16 | - |
| `144-pathcchremoveextension` | LANDS | PASS | 2.710 | 1.410 @ 1024 | - |
| `145-swab` | LANDS | PASS | 7.592 | 2.300 @ 16B | - |
| `146-memccpy` | LANDS | PASS | 8.936 | 3.580 @ 16B/absent | - |
| `147-strtok-s` | LANDS | PASS | 9.508 | 2.560 @ 16B/1tok | - |
| `148-wcstok-s` | LANDS | PASS | 5.652 | 1.680 @ 254ch/csv | - |
| `149-wcsrchr` | LANDS | PASS | 1.825 | 1.320 @ 16/miss | - |
| `150-strcpy-s` | LANDS | PASS | 4.100 | - @ - | - |
| `151-wcscpy-s` | LANDS | PASS | 2.877 | - @ - | - |
| `152-strcat-s` | LANDS | PASS | 5.387 | - @ - | - |
| `153-wcscat-s` | LANDS | PASS | 3.240 | - @ - | - |
| `154-strncpy-s` | LANDS | PASS | 4.161 | - @ - | - |
| `155-wcsncpy-s` | LANDS | PASS | 2.787 | - @ - | - |
| `156-strncat-s` | LANDS | PASS | 5.664 | - @ - | - |
| `157-wcsncat-s` | LANDS | PASS | 2.868 | - @ - | - |
| `158-pathrenameextensionw` | LANDS | PASS | 3.903 | - @ - | - |
| `159-pathcchrenameextension` | LANDS | PASS | 3.546 | - @ - | - |
| `160-pathcchaddextension` | LANDS | PASS | 3.780 | - @ - | - |
| `161-pathfindfilenamew` | LANDS | PASS | 3.226 | - @ - | - |
| `162-pathstrippathw` | LANDS | PASS | 3.207 | - @ - | - |
| `163-pathremovefilespecw` | NO_BUILD | - | - | - @ - | - |
| `164-pathcchaddbackslash` | LANDS | PASS | 2.313 | - @ - | - |
| `165-rtlupperstring` | LANDS | PASS | 119.674 | - @ - | - |
| `166-rtlipv6stringtoaddressw` | LANDS | PASS | 3.964 | 1.860 @ loopback | - |
| `167-pathcommonprefixw` | LANDS | PASS | 14.899 | - @ - | - |
| `168-strcpynw` | LANDS | PASS | 4.107 | 1.740 @ 8 | - |
| `169-strchrnw` | LANDS | PASS | 4.026 | 1.360 @ 254/hit@2 | - |
| `170-strcatbuffw` | LANDS | PASS | 3.859 | 1.300 @ empty+16 | - |
| `171-pathremovebackslashw` | LANDS | PASS | 2.376 | 1.070 @ drive-root | - |
| `172-pathquotespacesw` | LANDS | PASS | 2.425 | 1.040 @ 16 | - |
| `173-pathfindnextcomponentw` | LANDS | PASS | 4.737 | 1.710 @ sep@2 | - |
| `174-pathundecoratew` | LANDS | PASS | 3.569 | 1.540 @ 16/dec | - |
| `175-pathremoveargsw` | LANDS | PASS | 6.297 | 2.440 @ realpath | - |
| `176-pathcchremovebackslash` | LANDS | PASS | 2.006 | 1.030 @ drive-root | - |
| `177-pathisprefixw` | LANDS | PASS | 27.891 | - @ - | - |
| `178-wcsupr-s` | LANDS | PASS | 3.490 | 1.780 @ 8 | - |
| `179-strlwr-s` | LANDS | PASS | 5.407 | 1.800 @ 8 | - |
| `180-wcslwr-s` | LANDS | PASS | 3.535 | 1.760 @ 8 | - |
| `181-strupr-s` | LANDS | PASS | 6.828 | 1.930 @ 8 | - |
| `182-strset-s` | REGRESSED | PASS | 2.460 | 0.830 @ 8 | 8=0.83x |
| `183-wcsset-s` | REGRESSED | PASS | 1.739 | 0.780 @ 8 | 8=0.78x |
| `184-strnset-s` | REGRESSED | PASS | 2.628 | 0.860 @ 8 | 8=0.86x |
| `185-wcsnset-s` | REGRESSED | PASS | 2.297 | 0.820 @ 8 | 8=0.82x |
| `186-wtoi` | LANDS | PASS | 2.666 | 1.870 @ ws+"+2147483647" | - |
| `187-wtoi64` | LANDS | PASS | 2.186 | 2.390 @ "-9223372036854775808" | - |
| `188-wcstol` | LANDS | PASS | 1.949 | - @ - | - |
| `189-wcstoul` | LANDS | PASS | 1.889 | - @ - | - |
| `190-wcstoi64` | LANDS | PASS | 1.662 | - @ - | - |
| `191-wcstoui64` | LANDS | PASS | 1.669 | - @ - | - |
| `192-rtlarebitsclear` | LANDS | PASS | 2.510 | 1.110 @ 32 | - |
| `193-rtlistextunicode` | LANDS | PASS | 5.742 | - @ - | - |
| `194-i64toa-s` | LANDS | PASS | 2.254 | - @ - | - |
| `195-ui64toa-s` | LANDS | PASS | 2.308 | - @ - | - |
| `196-i64tow-s` | LANDS | PASS | 2.182 | - @ - | - |
| `197-ui64tow-s` | LANDS | PASS | 2.066 | - @ - | - |
| `198-itoa-s` | LANDS | PASS | 1.725 | - @ - | - |
| `199-ultoa-s` | LANDS | PASS | 1.705 | - @ - | - |
| `200-itow-s` | LANDS | PASS | 1.609 | - @ - | - |
| `201-ultow-s` | LANDS | PASS | 1.692 | - @ - | - |
| `202-convertguidtostringw` | LANDS | PASS | 37.944 | - @ - | - |
| `203-convertguidtostringa` | LANDS | PASS | 35.357 | - @ - | - |
| `204-rtludiv128` | LANDS | PASS | 6.966 | - @ - | - |
| `205-uuidfromstringa` | LANDS | PASS | 8.383 | - @ - | - |
| `206-stringfromguid2` | LANDS | PASS | 3.323 | - @ - | - |
| `207-iidfromstring` | LANDS | PASS | 3.747 | - @ - | - |
| `208-uuidfromstringw` | LANDS | PASS | 2.754 | - @ - | - |
| `209-lstrcpynw` | LANDS | PASS | 3.452 | - @ - | - |
| `210-comparestringordinal` | LANDS | PASS | 3.453 | - @ - | - |
| `211-lstrcpyna` | LANDS | PASS | 6.874 | - @ - | - |
| `212-pathfindfilenamea` | LANDS | PASS | 19.101 | - @ - | - |
| `213-strrchra` | LANDS | PASS | 94.467 | 56.030 @ 64/miss/unb | - |
| `214-strcspna` | LANDS | PASS | 111.585 | 24.680 @ 16/set3 | - |
| `215-strpbrka` | LANDS | PASS | 102.480 | 15.980 @ 16/set3 | - |
| `216-strspna` | LANDS | PASS | 145.753 | 21.540 @ 16/set23 | - |
| `217-pathfindextensiona` | LANDS | PASS | 28.941 | 9.760 @ 16 | - |
| `218-strtrima` | LANDS | PASS | 106.486 | 60.080 @ 64/notrim | - |
| `219-pathstrippatha` | LANDS | PASS | 13.345 | - @ - | - |
| `220-strchra` | LANDS | PASS | 10.107 | 3.870 @ 8 | - |
| `221-pathremoveblanksa` | LANDS | PASS | 16.736 | 3.870 @ 16 | - |
| `222-pathremoveextensiona` | LANDS | PASS | 12.726 | 4.460 @ 16 | - |
| `223-pathundecoratea` | LANDS | PASS | 18.723 | 4.820 @ 16/dec | - |
| `224-pathrenameextensiona` | LANDS | PASS | 17.172 | - @ - | - |
| `225-lstrlena` | LANDS | PASS | 3.191 | - @ - | - |
| `226-pathremoveargsa` | LANDS | PASS | 28.221 | - @ - | - |
| `227-lstrcpya` | LANDS | PASS | 8.107 | - @ - | - |
| `228-lstrcata` | LANDS | PASS | 5.105 | - @ - | - |
| `229-lstrcpyw` | LANDS | PASS | 3.078 | - @ - | - |
| `230-lstrcatw` | LANDS | PASS | 5.040 | - @ - | - |
| `231-strcatbuffa` | LANDS | PASS | 4.438 | - @ - | - |
| `232-pathremovebackslasha` | LANDS | PASS | 4.441 | - @ - | - |
| `233-pathquotespacesa` | LANDS | PASS | 3.740 | - @ - | - |
| `234-pathfindnextcomponenta` | LANDS | PASS | 9.070 | - @ - | - |
| `235-pathisfilespeca` | LANDS | PASS | 37.139 | - @ - | - |
| `236-pathcommonprefixa` | LANDS | PASS | 65.588 | - @ - | - |
| `237-pathisprefixa` | LANDS | PASS | 90.245 | - @ - | - |
| `238-pathmakeprettya` | LANDS | PASS | 24.202 | - @ - | - |
| `239-pathmatchspeca` | NO_BUILD | - | - | - @ - | - |
| `240-pathcchremovefilespec` | LANDS | PASS | 5.188 | - @ - | - |
| `241-pathcchaddbackslashex` | LANDS | PASS | 5.299 | - @ - | - |
| `242-pathcchappendex` | LANDS | PASS | 12.704 | - @ - | - |
| `243-pathcchcanonicalizeex` | LANDS | PASS | 14.312 | - @ - | - |
| `244-hashdata` | LANDS | PASS | 1.942 | - @ - | - |
| `245-urlunescapew` | LANDS | PASS | 5.631 | - @ - | - |
| `246-pathcanonicalizew` | LANDS | PASS | 6.586 | 12.310 @ C:\dir\file.txt | - |
| `247-pathaddextensionw` | LANDS | PASS | 3.162 | - @ - | - |
| `248-urlunescapea` | LANDS | PASS | 8.864 | - @ - | - |
| `249-urlhasha` | LANDS | PASS | 1.633 | - @ - | - |
| `250-rtlipv6stringtoaddressexw` | LANDS | PASS | 2.586 | 1.530 @ :: | - |
| `251-pathissamerootw` | LANDS | PASS | 49.943 | - @ - | - |
| `252-rtlfindunicodesubstring` | LANDS | PASS | 15.699 | - @ - | - |
| `253-strcat` | LANDS | PASS | 1.726 | - @ - | - |
| `254-findstringordinal` | LANDS | PASS | 6.987 | - @ - | - |
| `255-rtlfindlongestrunclear` | LANDS | PASS | 5.517 | - @ - | - |
| `256-rtlfindsetbits` | LANDS | PASS | 2.912 | - @ - | - |
| `257-rtlnumberofsetbits` | LANDS | PASS | 1.980 | - @ - | - |
| `258-rtlfindclearruns` | LANDS | PASS | 4.116 | - @ - | - |
| `259-rtlarebitsset` | LANDS | PASS | 2.313 | - @ - | - |
| `260-rtlcopybitmap` | LANDS | PASS | 2.918 | - @ - | - |
| `261-rtlfindnextforwardrunclear` | LANDS | PASS | 4.540 | - @ - | - |
| `262-rtlfindsetbitsandclear` | LANDS | PASS | 2.252 | - @ - | - |
| `263-rtlcompareunicodestrings` | LANDS | PASS | 6.037 | - @ - | - |
| `264-rtlinitutf8string` | LANDS | PASS | 3.640 | - @ - | - |
| `265-rtlappendasciiztostring` | LANDS | PASS | 2.462 | - @ - | - |
| `266-rtliszeromemory` | LANDS | PASS | 3.430 | - @ - | - |
| `267-rtlcrc32` | LANDS | PASS | 2.264 | - @ - | - |
| `268-rtlunicodestringtoutf8string` | LANDS | PASS | 3.275 | - @ - | - |
| `269-convertstringsidtosid` | LANDS | PASS | 6.252 | - @ - | - |
| `270-convertsidtostringsid` | LANDS | PASS | 2.980 | - @ - | - |
| `271-convertsidtostringsida` | LANDS | PASS | 4.207 | - @ - | - |
| `272-convertstringsidtosida` | LANDS | PASS | 6.465 | - @ - | - |
| `273-inet-addr` | LANDS | PASS | 2.747 | 2.210 @ 255.255.255.254 | - |
| `274-sysallocstring` | LANDS | PASS | 2.069 | - @ - | - |
| `275-inet-ntoa` | LANDS | PASS | 4.046 | 3.770 @ 10.10.10.10 | - |
| `276-varbstrcmp` | LANDS | PASS | 2.352 | - @ - | - |
| `277-charupperbuffw` | LANDS | PASS | 7.419 | - @ - | - |
| `278-rtlintegertounicodestring` | LANDS | PASS | 7.447 | - @ - | - |
| `279-rtlintegertochar` | LANDS | PASS | 2.627 | - @ - | - |
| `280-rtllargeintegertochar` | LANDS | PASS | 4.002 | - @ - | - |
| `281-strchriw` | LANDS | PASS | 179.090 | - @ - | - |
| `282-strrchriw` | LANDS | PASS | 290.304 | - @ - | - |
| `283-strrstriw` | LANDS | PASS | 69.346 | - @ - | - |
| `284-strstriw` | LANDS | PASS | 96.647 | - @ - | - |
| `285-strcspniw` | LANDS | PASS | 89.145 | - @ - | - |
| `286-strchrniw` | LANDS | PASS | 125.024 | - @ - | - |
| `287-getstringtypew` | LANDS | PASS | 3.818 | - @ - | - |
| `288-foldstringw-digits` | LANDS | PASS | 5.048 | - @ - | - |

