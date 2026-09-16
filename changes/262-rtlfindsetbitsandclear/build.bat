@echo off
REM changes/262-rtlfindsetbitsandclear/build.bat -- assemble, gate on correctness, then benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
rem  THIS CHANGE LINKS CHANGE 256. The search half is not reimplemented here: probes/equiv.c
rem  measured RtlFindSetBitsAndClear against RtlFindSetBits over a planted-run sweep plus 320000
rem  randomised and wrapped calls with ZERO disagreements, so what is new is the mutation and only
rem  the mutation. 256's impl.asm is assembled into its own .obj and linked.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep256.obj "%H%..\256-rtlfindsetbits\impl.asm" >nul || goto :err
rem
rem  THE CORRECTNESS GATE IS BUILT OPTIMISED, /O2, DELIBERATELY. Change 257 shipped a draft that
rem  passed this gate at /Od and died with an access violation at /O2: it clobbered a non-volatile
rem  register, and /Od had spilled everything so nothing was live there to destroy.
cl /nologo /O2 correctness.c reference.c impl.obj dep256.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj dep256.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
