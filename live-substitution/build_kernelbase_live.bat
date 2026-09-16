@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for changes 209, 210, 211, 225, 227, 229, 240 and 243 -- lstrcpynW + CompareStringOrdinal
REM  + lstrcpynA + lstrlenA + lstrcpyA + lstrcpyW.
REM  A fifth of the corpus is an unterminated source at a PAGE_NOACCESS page:
REM  the export swallows the fault, returns NULL and leaves a partial copy, and
REM  ours must match both the return and exactly how much it copied first.
REM  Sacrificial single-threaded child; no system process is touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Folcpn.obj "%C%\209-lstrcpynw\impl.asm" >nul || goto :err
ml64 /nologo /c /Focso.obj "%C%\210-comparestringordinal\impl.asm" >nul || goto :err
ml64 /nologo /c /Folcpna.obj "%C%\211-lstrcpyna\impl.asm" >nul || goto :err
ml64 /nologo /c /Folena.obj "%C%\225-lstrlena\impl.asm" >nul || goto :err
ml64 /nologo /c /Folcpa.obj "%C%\227-lstrcpya\impl.asm" >nul || goto :err
ml64 /nologo /c /Folcpw2.obj "%C%\229-lstrcpyw\impl.asm" >nul || goto :err
ml64 /nologo /c /Folcata.obj "%C%\228-lstrcata\impl.asm" >nul || goto :err
ml64 /nologo /c /Folcatw.obj "%C%\230-lstrcatw\impl.asm" >nul || goto :err
ml64 /nologo /c /Fohash.obj "%C%\244-hashdata\impl.asm" >nul || goto :err
ml64 /nologo /c /Founes.obj "%C%\245-urlunescapew\impl.asm" >nul || goto :err
ml64 /nologo /c /Fopcan.obj "%C%\246-pathcanonicalizew\impl.asm" >nul || goto :err
ml64 /nologo /c /Foaddx.obj "%C%\247-pathaddextensionw\impl.asm" >nul || goto :err
ml64 /nologo /c /Founea.obj "%C%\248-urlunescapea\impl.asm" >nul || goto :err
REM  249 (UrlHashA) is an envelope over TWO changes already linked here -- 225 for the length,
REM  including its SEH wrapper, and 244 for the hash -- so only its own six instructions are new.
ml64 /nologo /c /Fouh.obj "%C%\249-urlhasha\impl.asm" >nul || goto :err
REM  167 (PathCommonPrefixW) folds case with change 210's OS-built RtlUpcaseUnicodeChar table,
REM  which this harness already compiles for change 210.
ml64 /nologo /c /Fopcp.obj "%C%\167-pathcommonprefixw\impl.asm" >nul || goto :err
REM  177 (PathIsPrefixW) is an envelope over 167 and over change 001's wcslen.
ml64 /nologo /c /Fopip.obj "%C%\177-pathisprefixw\impl.asm" >nul || goto :err
ml64 /nologo /c /Fowcl.obj "%C%\001-wcslen\impl.asm" >nul || goto :err
REM  247's append point is change 132's rule, so 132's assembly is linked in too -- the same
REM  arrangement 246 has with 243.
ml64 /nologo /c /Fopfe132.obj "%C%\132-pathfindextensionw\impl.asm" >nul || goto :err
ml64 /nologo /c /Foprfs.obj "%C%\240-pathcchremovefilespec\impl.asm" >nul || goto :err
ml64 /nologo /c /Fopccx.obj "%C%\243-pathcchcanonicalizeex\impl.asm" >nul || goto :err
ml64 /nologo /c /Fopcap.obj "%C%\242-pathcchappendex\impl.asm" >nul || goto :err
ml64 /nologo /c /Fopcbs.obj "%C%\241-pathcchaddbackslashex\impl.asm" >nul || goto :err
REM  209 and 211 both call their exception wrapper seh.c. Compiled in one cl command they
REM  would both land on seh.obj and the second would overwrite the first, so each gets its
REM  own object name.
cl /nologo /O2 /MD /c /Foseh209.obj "%C%\209-lstrcpynw\seh.c" >nul || goto :err
cl /nologo /O2 /MD /c /Foseh211.obj "%C%\211-lstrcpyna\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa /c /Foseh225.obj "%C%\225-lstrlena\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa /c /Foseh227.obj "%C%\227-lstrcpya\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa /c /Foseh229.obj "%C%\229-lstrcpyw\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa /c /Foseh228.obj "%C%\228-lstrcata\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa /c /Foseh230.obj "%C%\230-lstrcatw\seh.c" >nul || goto :err
REM  248 keeps its envelope in seh.c too: the INPLACE-before-validation ordering, the AS_UTF8
REM  refusal, the __try that reproduces lstrlenA swallowing an access violation, and the staging
REM  buffer for the one overlap direction a forward single pass cannot do.
cl /nologo /O2 /MD /EHa /c /Foseh248.obj "%C%\248-urlunescapea\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa live_subst_kernelbase.c "%C%\210-comparestringordinal\wrapper.c" "%C%\210-comparestringordinal\upcase.c" seh209.obj seh211.obj seh225.obj seh227.obj seh229.obj seh228.obj seh230.obj seh248.obj lcpn.obj cso.obj lcpna.obj lena.obj lcpa.obj lcpw2.obj lcata.obj lcatw.obj hash.obj unes.obj unea.obj uh.obj pcp.obj pip.obj wcl.obj pcan.obj addx.obj pfe132.obj prfs.obj pccx.obj pcap.obj pcbs.obj /Fe:live_subst_kernelbase.exe >nul || goto :err
"%H%live_subst_kernelbase.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
