@echo off
REM ===========================================================================
Rem  2ND PC build -- amd Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445
REM  Builds impl_2ndpc.asm against this change's UNMODIFIED correctness.c and
REM  bench.c, so the 2nd-PC variant passes exactly the same two gates as the
REM  original: bit-exact vs the live system export, then no regressed size class.
REM  The original build.bat / impl.asm are untouched and still build the 5950X
REM  (Zen 3) implementation of record.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl_2ndpc.asm >nul || goto :err
cl /nologo /O2 correctness.c impl_2ndpc.obj /Fe:correctness_2ndpc.exe >nul || goto :err
"%H%correctness_2ndpc.exe" || (echo CORRECTNESS FAILED & goto :err)
rem bench.c built /Od so MSVC can't hoist the pure-ish call out of the timing loop.
cl /nologo /Od /I "%H%..\..\harness" bench.c impl_2ndpc.obj /Fe:bench_2ndpc.exe >nul || goto :err
"%H%bench_2ndpc.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1

