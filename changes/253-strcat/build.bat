@echo off
REM changes/253-strcat/build.bat -- assemble, gate on correctness, then benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..
cd /d "%H%"
rem  The destination scan is NOT rewritten here: changes 032 (strlen) and 001 (wcslen) are already
rem  AVX2 routines in this repository, so they are assembled and linked rather than duplicated.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep032.obj "%C%\032-strlen\impl.asm" >nul || goto :err
ml64 /nologo /c /Fodep001.obj "%C%\001-wcslen\impl.asm" >nul || goto :err
cl /nologo /O2 /D_CRT_SECURE_NO_WARNINGS correctness.c reference.c impl.obj dep032.obj dep001.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /D_CRT_SECURE_NO_WARNINGS /I "%H%..\..\harness" bench.c impl.obj dep032.obj dep001.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
