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
set C=%H%..
cd /d "%H%"
rem  Three landed pieces are LINKED rather than rewritten: change 001 (wcslen) resolves a cch of -1,
rem  change 210 builds the OS upcase table once, and change 252's casemate.c derives from it the
rem  case-partner table the vector filter compares against. probes/gonogo.c is why that is valid
rem  here: this function's fold is EXACTLY RtlUpcaseUnicodeChar, 0 differences over 65536 units,
rem  despite a disassembly that short-circuits everything below 0xC0.
ml64 /nologo /c impl_tgl.asm >nul || goto :err
ml64 /nologo /c /Fodep001.obj "%C%\001-wcslen\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Fodep210up.obj "%C%\210-comparestringordinal\upcase.c" >nul || goto :err
cl /nologo /O2 /c /Fodep252cm.obj "%C%\252-rtlfindunicodesubstring\casemate.c" >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl_tgl.obj dep001.obj dep252cm.obj dep210up.obj /Fe:correctness_tgl.exe >nul || goto :err
"%H%correctness_tgl.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl_tgl.obj dep001.obj dep252cm.obj dep210up.obj /Fe:bench_tgl.exe >nul || goto :err
"%H%bench_tgl.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
