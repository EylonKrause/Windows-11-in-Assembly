@echo off
REM ===========================================================================
Rem  tgl variant build
REM  Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457
REM  Builds impl_tgl.asm against this change's UNMODIFIED reference.c,
REM  correctness.c and bench.c, so the variant passes exactly the same two
REM  gates as the original: bit-exact vs the live system export on this
REM  machine, then no regressed size class.
REM  This file is the parent build.bat with four artifact names substituted,
REM  so any /Od, /MD, extra .c or import library it needs is preserved.
REM  build.bat and impl.asm are untouched and still build the implementation
REM  of record.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
rem  Nothing is linked from another change: this is a bit-granular copy, and the only
rem  dependency is the ISA -- AVX2, for the vector funnel shift.
rem
rem  THE CORRECTNESS GATE IS BUILT OPTIMISED, /O2, DELIBERATELY. Change 257 shipped a draft that
rem  passed this gate at /Od and died with an access violation at /O2: it clobbered a non-volatile
rem  register, and /Od had spilled everything so nothing was live there to destroy.
ml64 /nologo /c impl_tgl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl_tgl.obj /Fe:correctness_tgl.exe >nul || goto :err
"%H%correctness_tgl.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl_tgl.obj /Fe:bench_tgl.exe >nul || goto :err
"%H%bench_tgl.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
