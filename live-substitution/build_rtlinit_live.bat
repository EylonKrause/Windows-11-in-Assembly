@echo off
REM ===========================================================================
REM  Live substitution for seven more ntdll routines:
REM    094 RtlInitUnicodeString  095 RtlInitString  096 RtlInitUnicodeStringEx
REM    098 RtlInitStringEx       026 RtlCompareMemoryUlong
REM    051 RtlFindCharInUnicodeString                101 RtlAppendUnicodeToString
REM
REM  051 is the only one with a helper (upcase.c, for its case-insensitive flag).
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fori094.obj "%C%\094-rtlinitunicodestring\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fori095.obj "%C%\095-rtlinitstring\impl.asm"               >nul || goto :err
ml64 /nologo /c /Fori096.obj "%C%\096-rtlinitunicodestringex\impl.asm"      >nul || goto :err
ml64 /nologo /c /Fori098.obj "%C%\098-rtlinitstringex\impl.asm"             >nul || goto :err
ml64 /nologo /c /Fori026.obj "%C%\026-rtlcomparememoryulong\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fori051.obj "%C%\051-rtlfindcharinunicodestring\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fori101.obj "%C%\101-rtlappendunicodetostring\impl.asm"    >nul || goto :err

cl /nologo /O2 /c /Foriupc.obj "%C%\051-rtlfindcharinunicodestring\upcase.c" >nul || goto :err

cl /nologo /O2 live_subst_rtlinit.c ri094.obj ri095.obj ri096.obj ri098.obj ri026.obj ^
   ri051.obj ri101.obj riupc.obj /Fe:live_subst_rtlinit.exe >nul || goto :err

"%H%live_subst_rtlinit.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
