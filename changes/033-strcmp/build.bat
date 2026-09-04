@echo off
REM changes/033-strcmp/build.bat  -- assemble, gate on correctness, then benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set HERE=%~dp0
set HARNESS=%HERE%..\..\harness
pushd "%HERE%"

ml64 /nologo /c impl.asm
if errorlevel 1 goto :err

cl /nologo /O2 correctness.c reference.c impl.obj /Fe:correctness.exe >nul
if errorlevel 1 goto :err
"%HERE%correctness.exe"
if errorlevel 1 (echo CORRECTNESS FAILED - not benchmarking & goto :err)

cl /nologo /O2 /I "%HARNESS%" bench.c impl.obj /Fe:bench.exe >nul
if errorlevel 1 goto :err
"%HERE%bench.exe"

popd
endlocal
exit /b 0
:err
echo BUILD/RUN ERROR
popd
endlocal
exit /b 1
