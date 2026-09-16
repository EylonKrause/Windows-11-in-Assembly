@echo off
REM changes/269-convertstringsidtosid/build.bat -- assemble, gate on correctness, benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
rem  THE CORRECTNESS GATE IS BUILT OPTIMISED, /O2, DELIBERATELY. Change 257 shipped a draft that
rem  passed at /Od and died with an access violation at /O2.
cl /nologo /O2 correctness.c reference.c aliases.c classify.c impl.obj advapi32.lib user32.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c aliases.c classify.c impl.obj advapi32.lib user32.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
