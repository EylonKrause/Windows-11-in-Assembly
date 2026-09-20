@echo off
REM ===========================================================================
Rem  tgl variant build
REM  Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457
REM  Builds impl_tgl.asm against this change's UNMODIFIED reference.c and
REM  correctness.c, so the CORRECTNESS gate is literally the parent's: the
REM  same oracle and the same live-export comparison.
REM
Rem  the bench is bench_tgl.c, not bench.c, and that is deliberate. The parent
REM  table is five homogeneous classes plus ASCII-alternating-with-2-byte,
REM  which is the one mixture the AVX2 file already has a kernel for -- so it
REM  cannot see the mixed-width classes this variant exists to fix, nor the
REM  malformed ones. bench_tgl.c adds e0ed, a+3, a+4, 2+3, 1234, bad32 and
REM  rand, and runs every row at the worst alignment. bench.c is UNEDITED and
REM  still builds as bench_parent_tgl.exe below, so both tables are on record.
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
cl /nologo /O2 correctness.c reference.c impl_tgl.obj /Fe:correctness_tgl.exe >nul || goto :err
"%H%correctness_tgl.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench_tgl.c reference.c impl_tgl.obj /Fe:bench_tgl.exe >nul || goto :err
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c impl_tgl.obj /Fe:bench_parent_tgl.exe >nul || goto :err
"%H%bench_parent_tgl.exe"
"%H%bench_tgl.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
