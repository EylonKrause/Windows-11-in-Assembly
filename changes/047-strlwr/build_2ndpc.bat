@echo off
REM ===========================================================================
REM  2ND PC BUILD -- AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445
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
cl /nologo /O2 correctness.c reference.c impl_2ndpc.obj /Fe:correctness_2ndpc.exe >nul || goto :err
"%H%correctness_2ndpc.exe" || (echo CORRECTNESS FAILED & goto :err)
rem bench.c is built /Od on purpose: wia_wcspbrk is a pure, side-effect-free function
rem with loop-invariant args, so at /O2 MSVC devirtualizes the harness call and hoists
rem it clean out of the timing loop (bogus "0.00 ns"). /Od forces a real call each
rem iteration for BOTH our fn and the system fn; the identical loop overhead only
rem UNDERSTATES our win, so the LANDS gate stays honest. (impl_2ndpc.asm is native asm; the
rem reference/harness overhead is symmetric.) See RESULTS.md for rdtscp cross-checks.
cl /nologo /Od /I "%H%..\..\harness" bench.c reference.c impl_2ndpc.obj /Fe:bench_2ndpc.exe >nul || goto :err
"%H%bench_2ndpc.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1

