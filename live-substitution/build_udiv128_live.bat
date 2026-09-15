@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 204 -- ntdll!RtlUdiv128.
REM  The corpus is weighted onto DividendHigh == Divisor, the boundary where a
REM  hardware div would #DE, and onto Divisor == 0, which must not fault.
REM  Sacrificial single-threaded child; no system process is touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Foudiv128.obj "%C%\204-rtludiv128\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_udiv128.c udiv128.obj /Fe:live_subst_udiv128.exe >nul || goto :err
"%H%live_subst_udiv128.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
