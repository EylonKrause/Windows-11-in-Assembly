@echo off
REM changes/271-convertsidtostringsida/build.bat: assemble, gate on correctness, benchmark.
REM
Rem  three changes are linked here and none of them is copied: the formatter is change 067, the
REM  LocalAlloc and the four SetLastError calls are change 270's alloc.c, and only the narrowing is
REM  new. probes/contract.c established that the ANSI form is the wide form narrowed one byte per
REM  character, over every shape of SID and under four thread locales, with zero differences.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set D=%H%..\067-rtlconvertsidtounicodestring
set E=%H%..\270-convertsidtostringsid
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep067.obj "%D%\impl.asm" >nul || goto :err
rem  Both dependencies have a reference.c, and cl names its object after the source, so they are
rem  compiled separately into named objects -- otherwise the link silently drops one of them.
cl /nologo /O2 /c /Fodep067ref.obj "%D%\reference.c" >nul || goto :err
cl /nologo /O2 /EHa /c /Fodep067prb.obj "%D%\probe.c" >nul || goto :err
cl /nologo /O2 /c /Fodep270alc.obj "%E%\alloc.c" >nul || goto :err
rem  /EHa: the correctness gate puts a SID against a guard page and catches the fault.
cl /nologo /O2 /EHa correctness.c reference.c impl.obj dep067.obj dep067ref.obj dep067prb.obj dep270alc.obj advapi32.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c impl.obj dep067.obj dep067ref.obj dep067prb.obj dep270alc.obj advapi32.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
