@echo off
REM changes/285-strcspniw/build.bat: assemble, gate on correctness, benchmark.
REM
REM The match relation, its generated tables and the init that checks them against the live export all
REM come from change 281 unchanged, and that is measured, not assumed: probes/relation.c extracted
REM StrCSpnIW's own relation over 786420 pairs and found zero disagreements with change 281's tables.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set S=%H%..\281-strchriw
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c "%S%\tables.c" "%S%\foldsets.c" "%S%\foldbig.c" "%S%\foldnul.c" impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c "%S%\tables.c" "%S%\foldsets.c" "%S%\foldbig.c" "%S%\foldnul.c" impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
