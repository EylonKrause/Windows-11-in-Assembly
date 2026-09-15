@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 209 -- kernelbase!lstrcpynW.
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
cl /nologo /O2 /MD live_subst_kernelbase.c "%C%\209-lstrcpynw\seh.c" lcpn.obj /Fe:live_subst_kernelbase.exe >nul || goto :err
"%H%live_subst_kernelbase.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
