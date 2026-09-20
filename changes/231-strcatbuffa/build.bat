@echo off
REM changes/231-strcatbuffa/build.bat -- assemble, gate on correctness, then benchmark.
Rem /EHa is for the correctness harness only: the shipped export faults when the caller lies
REM about cch (probes/scb2.c), so the harness must catch that to compare it. The implementation
REM itself has NO wrapper -- see impl.asm.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 /EHa correctness.c reference.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
