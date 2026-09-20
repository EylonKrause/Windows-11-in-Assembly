@echo off
REM changes/297-windowscomparestringordinal/build.bat: assemble, gate on correctness, then benchmark.
REM The correctness gate is hard: the bench is never built if it fails.
REM runtimeobject.lib is here for RoOriginateErrorW, the shipped export originates a WinRT error
REM on its E_INVALIDARG path and correctness.c compares the IRestrictedErrorInfo it leaves behind.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj runtimeobject.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj runtimeobject.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
