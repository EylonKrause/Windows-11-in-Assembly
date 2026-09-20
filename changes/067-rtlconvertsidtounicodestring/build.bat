@echo off
REM changes/067-rtlconvertsidtounicodestring/build.bat: assemble, gate on correctness, benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
rem  /EHa: the correctness gate puts a SID against a guard page and catches the fault, because a
rem  short SID is a REFUSAL when its sub-authority array runs off the end and a FAULT when only its
rem  identifier authority does -- see probe.c.
cl /nologo /O2 /EHa correctness.c reference.c probe.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
rem  THE BENCH IS BUILT /O2. The version this replaces was built /Od with `#pragma optimize("",off)`
rem  around the two operations, which times the harness as much as the function.
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c probe.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
