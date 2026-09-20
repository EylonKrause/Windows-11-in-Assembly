@echo off
REM changes/276-varbstrcmp/build.bat -- assemble, gate on correctness, benchmark.
REM
Rem  the collation is the os's. probes/contract.c established that VarBstrCmp is CompareStringW, so
REM  flags.c calls it and this change owns only the wrapper: the empty rules, the validation, and a
REM  fast path for operands that are byte-identical -- which the export does not have, and which
REM  probes/gap.c measured at 0.8 ns per character of pure waste.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c flags.c impl.obj oleaut32.lib kernel32.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c flags.c impl.obj oleaut32.lib kernel32.lib user32.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
