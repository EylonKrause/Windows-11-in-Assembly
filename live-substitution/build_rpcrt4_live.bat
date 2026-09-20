@echo off
REM ===========================================================================
Rem  live-run proof for changes 205 and 208 -- UuidFromStringA + UuidFromStringW.
REM  Every case compares all sixteen output bytes from a pre-poisoned GUID,
REM  failing cases included: the contract leaves the output untouched on error.
REM  Sacrificial single-threaded child; no system process is touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Foufsa.obj "%C%\205-uuidfromstringa\impl.asm" >nul || goto :err
ml64 /nologo /c /Foufsw.obj "%C%\208-uuidfromstringw\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_rpcrt4.c ufsa.obj ufsw.obj rpcrt4.lib /Fe:live_subst_rpcrt4.exe >nul || goto :err
"%H%live_subst_rpcrt4.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
