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
set LIST=%LIST% 142-pathaddbackslashw 228-lstrcata 230-lstrcatw 209-lstrcpynw 210-comparestringordinal 211-lstrcpyna 212-pathfindfilenamea 213-strrchra 214-strcspna 215-strpbrka 216-strspna 217-pathfindextensiona 218-strtrima 219-pathstrippatha 220-strchra 221-pathremoveblanksa 222-pathremoveextensiona 223-pathundecoratea 224-pathrenameextensiona 225-lstrlena 226-pathremoveargsa 227-lstrcpya 229-lstrcpyw 231-strcatbuffa 232-pathremovebackslasha 233-pathquotespacesa 234-pathfindnextcomponenta 235-pathisfilespeca 236-pathcommonprefixa 237-pathisprefixa 238-pathmakeprettya 240-pathcchremovefilespec 241-pathcchaddbackslashex 242-pathcchappendex 243-pathcchcanonicalizeex 244-hashdata 245-urlunescapew 246-pathcanonicalizew 247-pathaddextensionw 248-urlunescapea 249-urlhasha 250-rtlipv6stringtoaddressexw 167-pathcommonprefixw 177-pathisprefixw 251-pathissamerootw 252-rtlfindunicodesubstring

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
