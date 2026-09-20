@echo off
REM changes/288-foldstringw-digits/build.bat -- assemble, gate on correctness, benchmark.
REM
Rem this change implements one of FoldStringW's five flag paths, and the scope was measured rather than
REM chosen. probes/contract.c folded every code unit under every flag: MAP_FOLDDIGITS is the only
REM strictly 1:1 one (0 grew, 0 shrank, 0 refused, 462 changed), while MAP_FOLDCZONE turns one unit into
REM as many as EIGHTEEN and MAP_COMPOSITE grows 12197 of them. A mapping that changes the length is not
REM a per-character table, so those flags are separate problems -- the same way crypt32
REM !CryptBinaryToStringA is covered by four separate changes, one per output format.
REM
REM The table is derived from the live export at init and re-checked in bulk, and MAP_FOLDDIGITS was
REM confirmed CONTEXT-FREE (20000 random strings, 0 disagreements, no length ever changing) and
REM LOCALE-INVARIANT (seven thread locales, 0 entries different).
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
