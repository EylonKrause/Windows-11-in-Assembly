@echo off
REM changes/242-pathcchappendex/build.bat -- assemble, gate on correctness, then benchmark.
REM The oracle is this change's join composed with change 243's canonicalisation model, so both
REM reference.c files are compiled -- to DISTINCT object names, because they share a file name and the
REM second would otherwise overwrite the first.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 /c /Foref242.obj reference.c >nul || goto :err
cl /nologo /O2 /c /Foref243.obj "%H%..\243-pathcchcanonicalizeex\reference.c" >nul || goto :err
cl /nologo /O2 correctness.c impl.obj ref242.obj ref243.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
