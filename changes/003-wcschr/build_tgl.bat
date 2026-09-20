@echo off
REM ===========================================================================
REM  TGL VARIANT BUILD
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
set HERE=%~dp0
set HARNESS=%HERE%..\..\harness
pushd "%HERE%"

ml64 /nologo /c impl_tgl.asm
if errorlevel 1 goto :err

cl /nologo /O2 correctness.c reference.c impl_tgl.obj /Fe:correctness_tgl.exe >nul
if errorlevel 1 goto :err
"%HERE%correctness_tgl.exe"
if errorlevel 1 (echo CORRECTNESS FAILED - not benchmarking & goto :err)

cl /nologo /O2 /I "%HARNESS%" bench.c impl_tgl.obj /Fe:bench_tgl.exe >nul
if errorlevel 1 goto :err
"%HERE%bench_tgl.exe"

popd
endlocal
exit /b 0
:err
echo BUILD/RUN ERROR
popd
endlocal
exit /b 1
