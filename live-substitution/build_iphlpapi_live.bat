@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 202 -- iphlpapi!ConvertGuidToStringW.
REM  Sacrificial single-threaded child; no system process is touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Focgs.obj "%C%\202-convertguidtostringw\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_iphlpapi.c cgs.obj /Fe:live_subst_iphlpapi.exe >nul || goto :err
"%H%live_subst_iphlpapi.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
