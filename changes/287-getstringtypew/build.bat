@echo off
REM changes/287-getstringtypew/build.bat -- assemble, gate on correctness, benchmark.
REM
REM The three classification tables are DERIVED from the live export at init by tables.c and re-checked
REM against it -- every one of the 65536 entries back through both levels of the directory-and-page
REM layout, and then 511-unit BULK calls re-checked against the per-character extraction, because the
REM bulk call is what this change replaces.
REM
REM That a table is legal at all was measured, not assumed: probes/contract.c found the classification
REM CONTEXT-FREE (20000 random strings up to 2048 units, 0 disagreements, all three info types) and
REM LOCALE-INVARIANT (the whole CT_CTYPE1 table rebuilt under seven thread locales, 0 entries different).
REM Either of those failing would have killed the change the way collation killed 274 and 276.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c tables.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c tables.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
