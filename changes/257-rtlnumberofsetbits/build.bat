@echo off
REM changes/257-rtlnumberofsetbits/build.bat: assemble, gate on correctness, then benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
rem  Nothing is linked from another change: this is pure bit counting, with no table, no fold and
rem  no length routine. The only dependency is the ISA -- AVX2 (vpshufb, vpsadbw) and POPCNT.
rem
rem  correctness.c MUST be built OPTIMISED. It once passed at /Od and crashed at /O2, and the fault
rem  was real: the entry stubs set their selector in r13, a NON-VOLATILE register, before the framed
rem  body's prologue saved it. /Od spills everything and survived; /O2 keeps live values there and
rem  did not. Building the gate unoptimised would have hidden an ABI violation.
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
