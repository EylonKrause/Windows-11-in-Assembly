@echo off
REM changes/270-convertsidtostringsid/build.bat: assemble, gate on correctness, benchmark.
REM
Rem  this change links change 067 Rather than copying it. probes/contract.c established that
REM  advapi32!ConvertSidToStringSidW and ntdll!RtlConvertSidToUnicodeString produce the same text for
REM  every shape of SID and refuse the same ones, so the formatter here IS change 067, the same way
REM  change 268 links 016 and 034. Pasting a formatter in would create a second copy that a future
REM  correction would silently leave behind.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set D=%H%..\067-rtlconvertsidtounicodestring
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep067.obj "%D%\impl.asm" >nul || goto :err
rem  BOTH CHANGES HAVE A reference.c, and cl names its object after the source, so compiling them in
rem  one command silently drops one of them ("object specified more than once; extras ignored") and
rem  the link fails on ref_sidfmt. Change 067's two C files are therefore compiled separately, into
rem  named objects.
cl /nologo /O2 /c /Fodep067ref.obj "%D%\reference.c" >nul || goto :err
cl /nologo /O2 /EHa /c /Fodep067prb.obj "%D%\probe.c" >nul || goto :err
rem  /EHa: the correctness gate puts a SID against a guard page and catches the fault, because a
rem  short SID is a REFUSAL when its sub-authority array runs off the end and a FAULT when only its
rem  identifier authority does.
cl /nologo /O2 /EHa correctness.c reference.c alloc.c impl.obj dep067.obj dep067ref.obj dep067prb.obj advapi32.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c alloc.c impl.obj dep067.obj dep067ref.obj dep067prb.obj advapi32.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
