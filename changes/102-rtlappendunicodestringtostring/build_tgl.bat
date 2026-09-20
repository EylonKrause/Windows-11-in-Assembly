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
ml64 /nologo /c impl_tgl.asm >nul || goto :err
cl /nologo /O2 correctness.c impl_tgl.obj /Fe:correctness_tgl.exe >nul || goto :err
"%H%correctness_tgl.exe" || (echo CORRECTNESS FAILED & goto :err)
rem bench.c built /Od so MSVC can't hoist the pure-ish call out of the timing loop.
cl /nologo /Od /I "%H%..\..\harness" bench.c impl_tgl.obj /Fe:bench_tgl.exe >nul || goto :err
"%H%bench_tgl.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
