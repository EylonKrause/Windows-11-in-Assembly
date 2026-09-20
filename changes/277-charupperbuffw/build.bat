@echo off
REM changes/277-charupperbuffw/build.bat: assemble, gate on correctness, benchmark.
REM
REM  tables.c builds both case tables by asking CharUpperBuffW and CharLowerBuffW themselves, one
REM  code unit at a time, and then checks the rule the vector path depends on: that below 0x80 the
REM  table agrees with the range rule. probes/mapping.c established the mapping is a pure
REM  per-character table (not locale-aware, no context) which is what makes this convertible
REM  where changes 274 and 276 were not.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c tables.c impl.obj user32.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c tables.c impl.obj user32.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
