@echo off
REM changes/228-lstrcata/build.bat -- assemble, gate on correctness, then benchmark.
REM /EHa is required: the wrapper in seh.c converts an access violation into NULL, which is the
REM measured behaviour of the shipped export (see probes/cata.c).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
REM Precompiled to a change-specific object name: several changes in this repository have a file
REM called seh.c, and one shared seh.obj silently loses whichever was compiled first (see 211).
cl /nologo /O2 /EHa /c seh.c /Fo:seh228.obj >nul || goto :err
cl /nologo /O2 /EHa correctness.c reference.c impl.obj seh228.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj seh228.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
