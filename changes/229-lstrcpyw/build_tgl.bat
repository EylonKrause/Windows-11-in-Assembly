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
REM Precompiled to a change-specific object name: several changes in this repository have a file
REM called seh.c, and one shared seh.obj silently loses whichever was compiled first (see 211).
cl /nologo /O2 /EHa /c seh.c /Fo:seh229.obj >nul || goto :err
cl /nologo /O2 /EHa correctness.c reference.c impl_tgl.obj seh229.obj /Fe:correctness_tgl.exe >nul || goto :err
"%H%correctness_tgl.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl_tgl.obj seh229.obj /Fe:bench_tgl.exe >nul || goto :err
"%H%bench_tgl.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
