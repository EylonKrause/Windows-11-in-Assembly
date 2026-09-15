@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for changes 206 and 207 -- StringFromGUID2 + IIDFromString.
REM  Refusals (cchMax <= 38) must write NOTHING, so every case compares the whole
REM  buffer from a poisoned baseline, refusals included.
REM  Sacrificial single-threaded child; no system process is touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fosfg2.obj "%C%\206-stringfromguid2\impl.asm" >nul || goto :err
ml64 /nologo /c /Foiidfs.obj "%C%\207-iidfromstring\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_combase.c sfg2.obj iidfs.obj /Fe:live_subst_combase.exe >nul || goto :err
"%H%live_subst_combase.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
