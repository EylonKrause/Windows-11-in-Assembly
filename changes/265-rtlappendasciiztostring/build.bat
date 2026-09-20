@echo off
REM changes/265-rtlappendasciiztostring/build.bat: assemble, gate on correctness, then benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
rem  Nothing is linked from another change: the page-safe AVX2 strlen is inlined, which is what
rem  changes 095 and 101 do for the same reason -- a call is most of the cost on a short append.
ml64 /nologo /c impl.asm >nul || goto :err
rem
rem  THE CORRECTNESS GATE IS BUILT OPTIMISED, /O2, DELIBERATELY. Change 257 shipped a draft that
rem  passed this gate at /Od and died with an access violation at /O2: it clobbered a non-volatile
rem  register, and /Od had spilled everything so nothing was live there to destroy.
cl /nologo /O2 correctness.c reference.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
