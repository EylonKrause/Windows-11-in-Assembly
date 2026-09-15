@echo off
REM changes/201-ultow-s/build.bat -- assemble, gate on correctness, then benchmark.
REM /MD is REQUIRED: errno and the invalid-parameter handler must be UCRTBASE's -- the same ones
REM our assembly writes through via its exported _errno / _invalid_parameter_noinfo.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 /MD correctness.c reference.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /MD /I "%H%..\..\harness" bench.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
