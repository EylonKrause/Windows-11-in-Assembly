@echo off
rem tools/abi-check/check.bat
rem ---------------------------------------------------------------------------------------------
rem Gate 3: prove an implementation preserves the Win64 non-volatile registers.
rem
rem Gates 1 and 2 (bit-exact correctness, and no size class below 0.97x) are both blind to this
rem class of bug: a function that uses xmm6 as scratch returns exactly the right bytes at exactly
rem the right speed, and corrupts only a caller that happened to have a live double. Sixteen
rem implementations here did that undetected until change 202's benchmark -- whose accumulators
rem sit in xmm6/xmm7 -- reported a correct function as taking 0.00 ns.
rem
rem   check.bat            check every change listed below
rem   check.bat 042        check one
rem
rem Exit 0 = every checked change preserved rbx rbp rdi rsi r12-r15 and the low 128 bits of
rem xmm6-xmm15, balanced the stack and left DF clear. Exit 1 = at least one did not.
rem ---------------------------------------------------------------------------------------------
setlocal enabledelayedexpansion
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"

ml64 /nologo /c abi_probe.asm >nul || (echo PROBE BUILD ERROR & exit /b 1)

rem Directory names only; a change id is their first three characters.
set LIST=042-wcsicmp 043-stricmp 044-wcsnicmp 045-strnicmp 046-memicmp
set LIST=%LIST% 047-strlwr 048-strupr 049-wcslwr 050-wcsupr
set LIST=%LIST% 051-rtlfindcharinunicodestring
set LIST=%LIST% 105-cryptstringtobinaryw-base64header 107-cryptstringtobinaryw-base64any
set LIST=%LIST% 125-rtlfindclearbits 132-pathfindextensionw 140-pathremoveextensionw 143-pathcchfindextension 144-pathcchremoveextension 158-pathrenameextensionw 159-pathcchrenameextension 160-pathcchaddextension 161-pathfindfilenamew 162-pathstrippathw 174-pathundecoratew
set LIST=%LIST% 202-convertguidtostringw 203-convertguidtostringa 204-rtludiv128
set LIST=%LIST% 205-uuidfromstringa 206-stringfromguid2 207-iidfromstring 208-uuidfromstringw
set LIST=%LIST% 142-pathaddbackslashw 228-lstrcata 230-lstrcatw 209-lstrcpynw 210-comparestringordinal 211-lstrcpyna 212-pathfindfilenamea 213-strrchra 214-strcspna 215-strpbrka 216-strspna 217-pathfindextensiona 218-strtrima 219-pathstrippatha 220-strchra 221-pathremoveblanksa 222-pathremoveextensiona 223-pathundecoratea 224-pathrenameextensiona 225-lstrlena 226-pathremoveargsa 227-lstrcpya 229-lstrcpyw 231-strcatbuffa 232-pathremovebackslasha 233-pathquotespacesa 234-pathfindnextcomponenta 235-pathisfilespeca 236-pathcommonprefixa 237-pathisprefixa 238-pathmakeprettya 240-pathcchremovefilespec 241-pathcchaddbackslashex 242-pathcchappendex 243-pathcchcanonicalizeex 244-hashdata 245-urlunescapew 246-pathcanonicalizew 247-pathaddextensionw 248-urlunescapea 249-urlhasha 250-rtlipv6stringtoaddressexw 167-pathcommonprefixw 177-pathisprefixw 251-pathissamerootw 252-rtlfindunicodesubstring 254-findstringordinal 255-rtlfindlongestrunclear 256-rtlfindsetbits 257-rtlnumberofsetbits 258-rtlfindclearruns 259-rtlarebitsset 261-rtlfindnextforwardrunclear 262-rtlfindsetbitsandclear 263-rtlcompareunicodestrings 264-rtlinitutf8string 265-rtlappendasciiztostring 266-rtliszeromemory 267-rtlcrc32 268-rtlunicodestringtoutf8string 027-rtlupcaseunicodetomultibyten 031-rtlupcaseunicodetooemn 269-convertstringsidtosid 067-rtlconvertsidtounicodestring 270-convertsidtostringsid 271-convertsidtostringsida 272-convertstringsidtosida 273-inet-addr 275-inet-ntoa 277-charupperbuffw 278-rtlintegertounicodestring 279-rtlintegertochar 280-rtllargeintegertochar 281-strchriw 282-strrchriw

set FAILED=0
set RAN=0
set ONLY=%~1
for %%D in (%LIST%) do call :one "%%D"

echo.
if %FAILED%==0 (
  echo ABI GATE: PASS  -- %RAN% change^(s^) checked, 0 violations
  endlocal & exit /b 0
)
echo ABI GATE: FAIL  -- %RAN% change^(s^) checked, %FAILED% violation^(s^)
endlocal & exit /b 1

:one
set DIR=%~1
set ID=%DIR:~0,3%
if not "%ONLY%"=="" if not "%ONLY%"=="%ID%" goto :eof
set /a RAN+=1
set P=%H%..\..\changes\%DIR%

pushd "%P%"
ml64 /nologo /c impl.asm >nul 2>&1
if errorlevel 1 (
  popd
  echo ABI: BUILD ERROR  %DIR%  ^(impl.asm did not assemble^)
  set /a FAILED+=1
  goto :eof
)
rem Some changes keep a lookup table in a companion source: 051 builds an OS upcase table and
rem 105/107 share a base64 reverse table. Link whichever of them this change actually has.
set EXTRA=
if exist upcase.c set EXTRA=!EXTRA! "%P%\upcase.c"
if exist revtab.c set EXTRA=!EXTRA! "%P%\revtab.c"
if exist seh.c    set EXTRA=!EXTRA! "%P%\seh.c"
if exist wrapper.c set EXTRA=!EXTRA! "%P%\wrapper.c"
popd

rem  And one change is an ENVELOPE over another: 246 (PathCanonicalizeW) is fourteen instructions
rem  around change 243's core, so 243's assembly has to be assembled and linked with it. Stated
rem  explicitly rather than guessed at, the same way the live-substitution build scripts list their
rem  objects.
rem  And 262 (RtlFindSetBitsAndClear) is an envelope over change 256: probes/equiv.c measured its
rem  search to be exactly RtlFindSetBits over 320000 calls, so 256's assembly is what it calls and
rem  256's assembly has to be linked with it.
if "%ID%"=="262" (
  pushd "%H%..\..\changes\256-rtlfindsetbits"
  ml64 /nologo /c /Fo"%H%dep256.obj" impl.asm >nul 2>&1
  popd
  set EXTRA=!EXTRA! "%H%dep256.obj"
)
rem  263 (RtlCompareUnicodeStrings) folds case through change 210's upcase table, which is built at
rem  run time from RtlUpcaseUnicodeChar so that this project has exactly one place where it decides
rem  what the fold is. The table has to be linked and INITIALISED -- the driver calls
rem  wia_upcase_init through SETUP().
if "%ID%"=="263" (
  cl /nologo /O2 /c /Fo"%H%dep210up.obj" "%H%..\..\changes\210-comparestringordinal\upcase.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep210up.obj"
)
rem  264 (RtlInitUTF8String) is a linker ALIAS of change 095: probes/equiv.c proved the two exports
rem  identical over 125883 cases, so the implementation is not copied and 095's object has to be
rem  linked for the alias to resolve.
if "%ID%"=="264" (
  pushd "%H%..\..\changes\095-rtlinitstring"
  ml64 /nologo /c /Fo"%H%dep095.obj" impl.asm >nul 2>&1
  popd
  set EXTRA=!EXTRA! "%H%dep095.obj"
)
rem  267 (RtlCrc32) needs its shift tables, which shifttab.c builds from the polynomial at run
rem  time -- the driver calls wia_crc32_tables_init through SETUP().
if "%ID%"=="267" (
  cl /nologo /O2 /c /Fo"%H%dep267tab.obj" "%H%..\..\changes\267-rtlcrc32\shifttab.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep267tab.obj"
)
rem  277 (CharUpperBuffW/CharLowerBuffW) needs both 64K case tables, and they are built by asking
rem  the exports themselves one code unit at a time -- so SETUP() ABORTS if they come back wrong,
rem  because a table that was the identity everywhere would be a wrong answer that looks right.
if "%ID%"=="277" (
  cl /nologo /O2 /c /Fo"%H%dep277tb.obj" "%H%..\..\changes\277-charupperbuffw\tables.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep277tb.obj" user32.lib
)
rem  275 (inet_ntoa) keeps its answer in a THREAD-LOCAL buffer, and tls.c owns it: the compiler
rem  emits the gs:[0x58] walk with the right relocations, which is not something to hand-write in
rem  MASM for the 0.38 ns probes/floor.c measured the whole access at.
if "%ID%"=="275" (
  cl /nologo /O2 /c /Fo"%H%dep275tls.obj" "%H%..\..\changes\275-inet-ntoa\tls.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep275tls.obj"
)
rem  272 (ConvertStringSidToSidA) is change 269's parser behind a widening, so 269's assembly and
rem  both of its OS-derived tables come with it, plus 272's own widen.c for MultiByteToWideChar and
rem  the temporary's allocation.
if "%ID%"=="272" (
  pushd "%H%..\..\changes\269-convertstringsidtosid"
  ml64 /nologo /c /Fo"%H%dep272p.obj" impl.asm >nul 2>&1
  popd
  cl /nologo /O2 /c /Fo"%H%dep272al.obj" "%H%..\..\changes\269-convertstringsidtosid\aliases.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep272cl.obj" "%H%..\..\changes\269-convertstringsidtosid\classify.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep272wd.obj" "%H%..\..\changes\272-convertstringsidtosida\widen.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep272p.obj" "%H%dep272al.obj" "%H%dep272cl.obj" "%H%dep272wd.obj" advapi32.lib user32.lib
)
rem  271 (ConvertSidToStringSidA) is the same envelope with a byte-narrowing pack: change 067's
rem  formatter, change 270's alloc.c for the LocalAlloc and the four SetLastError calls, and only the
rem  VPACKUSWB narrowing of its own.
if "%ID%"=="271" (
  pushd "%H%..\..\changes\067-rtlconvertsidtounicodestring"
  ml64 /nologo /c /Fo"%H%dep271fmt.obj" impl.asm >nul 2>&1
  popd
  cl /nologo /O2 /EHa /c /Fo"%H%dep271prb.obj" "%H%..\..\changes\067-rtlconvertsidtounicodestring\probe.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep271alc.obj" "%H%..\..\changes\270-convertsidtostringsid\alloc.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep271fmt.obj" "%H%dep271prb.obj" "%H%dep271alc.obj"
)
rem  270 (ConvertSidToStringSidW) is an ENVELOPE over change 067: probes/contract.c established that
rem  advapi32's export and ntdll's produce the same text for every shape of SID and refuse the same
rem  ones, so 067's assembly IS the formatter and has to be assembled and linked with it -- the same
rem  way 246 links 243 and 262 links 256. alloc.c owns the LocalAlloc and the four SetLastError
rem  calls; probe.c owns 067's protected header read.
if "%ID%"=="270" (
  pushd "%H%..\..\changes\067-rtlconvertsidtounicodestring"
  ml64 /nologo /c /Fo"%H%dep270fmt.obj" impl.asm >nul 2>&1
  popd
  cl /nologo /O2 /EHa /c /Fo"%H%dep270prb.obj" "%H%..\..\changes\067-rtlconvertsidtounicodestring\probe.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep270alc.obj" "%H%..\..\changes\270-convertsidtostringsid\alloc.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep270fmt.obj" "%H%dep270prb.obj" "%H%dep270alc.obj"
)
rem  067 (RtlConvertSidToUnicodeString) reads the revision, the count and the last sub-authority
rem  under an exception handler and the identifier authority without one, because that is what the
rem  live export does -- so probe.c, which owns that handler, has to be linked in.
if "%ID%"=="067" (
  cl /nologo /O2 /EHa /c /Fo"%H%dep067pr.obj" "%H%..\..\changes\067-rtlconvertsidtounicodestring\probe.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep067pr.obj"
)
rem  269 (ConvertStringSidToSidW) needs both of its OS-derived tables -- the SDDL aliases and the
rem  two character classes -- and both are INITIALISED through SETUP(), because a table that came
rem  back empty would turn every alias into ERROR_INVALID_SID.
if "%ID%"=="269" (
  cl /nologo /O2 /c /Fo"%H%dep269al.obj" "%H%..\..\changes\269-convertstringsidtosid\aliases.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep269cl.obj" "%H%..\..\changes\269-convertstringsidtosid\classify.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep269al.obj" "%H%dep269cl.obj" advapi32.lib user32.lib
)
rem  027 and 031 (RtlUpcaseUnicodeToMultiByteN / ToOemN) fold and narrow through a 65536-entry map
rem  built from the OS code page at run time, so the table object has to be linked and INITIALISED --
rem  the driver calls it through SETUP().
if "%ID%"=="027" (
  cl /nologo /O2 /c /Fo"%H%dep027map.obj" "%H%..\..\changes\027-rtlupcaseunicodetomultibyten\upansimap.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep027map.obj"
)
if "%ID%"=="031" (
  cl /nologo /O2 /c /Fo"%H%dep031map.obj" "%H%..\..\changes\031-rtlupcaseunicodetooemn\upoemmap.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep031map.obj"
)
rem  268 (RtlUnicodeStringToUTF8String / RtlUTF8StringToUnicodeString) is a WRAPPER over the two
rem  N-forms this project already converted -- 016 and 034 -- plus the one heap call its allocating
rem  path needs. Pasting either conversion in here would create a second copy that a future
rem  correction would silently leave behind, so all three objects are linked.
if "%ID%"=="268" (
  pushd "%H%..\..\changes\016-rtlunicodetoutf8n"
  ml64 /nologo /c /Fo"%H%dep016.obj" impl.asm >nul 2>&1
  popd
  pushd "%H%..\..\changes\034-rtlutf8tounicoden"
  ml64 /nologo /c /Fo"%H%dep034.obj" impl.asm >nul 2>&1
  popd
  cl /nologo /O2 /c /Fo"%H%dep268heap.obj" "%H%..\..\changes\268-rtlunicodestringtoutf8string\heapalloc.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep016.obj" "%H%dep034.obj" "%H%dep268heap.obj"
)
if "%ID%"=="246" (
  pushd "%H%..\..\changes\243-pathcchcanonicalizeex"
  ml64 /nologo /c /Fo"%H%dep243.obj" impl.asm >nul 2>&1
  popd
  set EXTRA=!EXTRA! "%H%dep243.obj"
)
rem  247 (PathAddExtensionW) is the same arrangement over change 132: the append point is
rem  PathFindExtensionW's rule, which 132 already gets right -- and which 132 SHIPPED WRONG once, so
rem  re-deriving it here rather than linking it would be repeating a known mistake.
if "%ID%"=="247" (
  pushd "%H%..\..\changes\132-pathfindextensionw"
  ml64 /nologo /c /Fo"%H%dep132.obj" impl.asm >nul 2>&1
  popd
  set EXTRA=!EXTRA! "%H%dep132.obj"
)
rem  249 (UrlHashA) is an envelope over TWO landed changes at once: 225 for the length, INCLUDING
rem  its SEH wrapper -- which is what makes a faulting URL return S_OK with the identity seed rather
rem  than crashing -- and 244 for the hash. Both objects, and 225's seh.c, are linked here.
if "%ID%"=="249" (
  pushd "%H%..\..\changes\225-lstrlena"
  ml64 /nologo /c /Fo"%H%dep225.obj" impl.asm >nul 2>&1
  cl /nologo /O2 /EHa /c /Fo"%H%dep225seh.obj" seh.c >nul 2>&1
  popd
  pushd "%H%..\..\changes\244-hashdata"
  ml64 /nologo /c /Fo"%H%dep244.obj" impl.asm >nul 2>&1
  popd
  set EXTRA=!EXTRA! "%H%dep225.obj" "%H%dep225seh.obj" "%H%dep244.obj"
)
rem  250 (RtlIpv6StringToAddressExW) is an envelope over change 166 -- the shipped ExW literally
rem  calls RtlIpv6StringToAddressW's own RVA -- so 166's assembly is linked alongside rather than
rem  re-derived. It is also the change that found, and fixed, a 0.79x regression inside 166 on "::".
if "%ID%"=="250" (
  pushd "%H%..\..\changes\166-rtlipv6stringtoaddressw"
  ml64 /nologo /c /Fo"%H%dep166.obj" impl.asm >nul 2>&1
  popd
  set EXTRA=!EXTRA! "%H%dep166.obj"
)
rem  167 (PathCommonPrefixW) folds case with change 210's OS-built RtlUpcaseUnicodeChar table --
rem  its own go/no-go measured 0 differences over 65534 code-unit pairs against 947 for a plain
rem  ASCII fold -- so 210's upcase.c is linked in and SETUP() builds the table before the probe runs.
if "%ID%"=="167" (
  cl /nologo /O2 /c /Fo"%H%dep210up.obj" "%H%..\..\changes\210-comparestringordinal\upcase.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep210up.obj"
)
rem  177 (PathIsPrefixW) is an envelope over TWO landed changes: 167 for the walk and 001 for the
rem  prefix's length. It was parked not as "we could not derive it" but as "we derived it, and it is
rem  PathCommonPrefixW, which is parked" -- so all three objects are linked here.
rem  251 (PathIsSameRootW) is the ROOT SKIP -- derived from the disassembly of PathCchSkipRoot and
rem  refuted against the live export over 210720 cases -- plus change 167's walk, so 167 and 210's
rem  upcase table are linked with it.
if "%ID%"=="251" (
  pushd "%H%..\..\changes\167-pathcommonprefixw"
  ml64 /nologo /c /Fo"%H%dep167.obj" impl.asm >nul 2>&1
  popd
  cl /nologo /O2 /c /Fo"%H%dep210up.obj" "%H%..\..\changes\210-comparestringordinal\upcase.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep167.obj" "%H%dep210up.obj"
)
rem  252 (RtlFindUnicodeSubstring) compares against the CASE-PARTNER table, which casemate.c derives
rem  from change 210's OS-built upcase table, so both objects are linked here.
rem  254 (FindStringOrdinal) links change 001 (wcslen, which resolves a cch of -1) and the
rem  case-partner table change 252 derives from change 210s OS-built upcase table. Valid here
rem  because probes/gonogo.c measured this function fold to be EXACTLY RtlUpcaseUnicodeChar.
if "%ID%"=="254" (
  pushd "%H%..\..\changes\001-wcslen"
  ml64 /nologo /c /Fo"%H%dep001.obj" impl.asm >nul 2>&1
  popd
  cl /nologo /O2 /c /Fo"%H%dep210up.obj" "%H%..\..\changes\210-comparestringordinal\upcase.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep252cm.obj" "%H%..\..\changes\252-rtlfindunicodesubstring\casemate.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep001.obj" "%H%dep252cm.obj" "%H%dep210up.obj"
)
rem  281 (StrChrIW) needs its generated match-relation tables and the init that checks them
rem  against the live export before the thunk runs.
rem  282 (StrRChrIW) links change 281's generated match-relation tables unchanged.
if "%ID%"=="282" (
  cl /nologo /O2 /c /Fo"%H%dep281t.obj" "%H%..\..\changes\281-strchriw\tables.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep281s.obj" "%H%..\..\changes\281-strchriw\foldsets.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep281b.obj" "%H%..\..\changes\281-strchriw\foldbig.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep281n.obj" "%H%..\..\changes\281-strchriw\foldnul.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep281t.obj" "%H%dep281s.obj" "%H%dep281b.obj" "%H%dep281n.obj"
)
if "%ID%"=="281" (
  cl /nologo /O2 /c /Fo"%H%dep281t.obj" "%H%..\..\changes\281-strchriw\tables.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep281s.obj" "%H%..\..\changes\281-strchriw\foldsets.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep281b.obj" "%H%..\..\changes\281-strchriw\foldbig.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep281n.obj" "%H%..\..\changes\281-strchriw\foldnul.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep281t.obj" "%H%dep281s.obj" "%H%dep281b.obj" "%H%dep281n.obj"
)
if "%ID%"=="252" (
  cl /nologo /O2 /c /Fo"%H%dep252cm.obj" "%H%..\..\changes\252-rtlfindunicodesubstring\casemate.c" >nul 2>&1
  cl /nologo /O2 /c /Fo"%H%dep210up.obj" "%H%..\..\changes\210-comparestringordinal\upcase.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep252cm.obj" "%H%dep210up.obj"
)
if "%ID%"=="177" (
  pushd "%H%..\..\changes\167-pathcommonprefixw"
  ml64 /nologo /c /Fo"%H%dep167.obj" impl.asm >nul 2>&1
  popd
  pushd "%H%..\..\changes\001-wcslen"
  ml64 /nologo /c /Fo"%H%dep001.obj" impl.asm >nul 2>&1
  popd
  cl /nologo /O2 /c /Fo"%H%dep210up.obj" "%H%..\..\changes\210-comparestringordinal\upcase.c" >nul 2>&1
  set EXTRA=!EXTRA! "%H%dep167.obj" "%H%dep001.obj" "%H%dep210up.obj"
)

cl /nologo /O2 /EHa /DT_%ID% abi_check.c abi_probe.obj "%P%\impl.obj" %EXTRA% /Fe:abi_%ID%.exe >nul 2>&1
if errorlevel 1 (
  echo ABI: BUILD ERROR  %DIR%  ^(driver did not link^)
  set /a FAILED+=1
  goto :eof
)
"%H%abi_%ID%.exe"
rem NOT "if errorlevel 1". A driver that CRASHES exits with an NTSTATUS -- 0xC0000005 for an access
rem violation -- which cmd sees as a NEGATIVE number, so "errorlevel 1" is false and the gate reported
rem PASS for a change whose driver never printed a line. 142's thunk did exactly that on its first run
rem (it called an export that has no NULL contract), and the gate said "65 changes checked, 0
rem violations" with 142 silently missing from the output. Anything other than 0 is a failure.
if not "%errorlevel%"=="0" (
  echo ABI: FAIL  %DIR%  ^(driver exited %errorlevel% -- crashed or reported a violation^)
  set /a FAILED+=1
)
goto :eof
