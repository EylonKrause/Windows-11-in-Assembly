@echo off
REM changes/178-wcsupr-s/build.bat -- assemble, gate on correctness, then benchmark.
Rem /md is required, not cosmetic: with the default static CRT this exe would carry its own
REM invalid-parameter handler state, so ucrtbase's _wcsupr_s and our _invalid_parameter_noinfo
REM would consult two different handlers -- the static one is unset, so the live export
REM __fastfails the process (observed: exit code 9, no output). Change 150 records the same.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 /MD correctness.c reference.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /MD /I "%H%..\..\harness" bench.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
