@echo off
REM changes/274-sysallocstring/build.bat: assemble, gate on correctness, benchmark.
REM
Rem  the allocation is called, not reimplemented. probes/contract.c established that a BSTR block
Rem  made by hand terminates the process when SysFreeString touches it, so oleaut32.lib is linked
REM  and SysAllocStringLen is tail-jumped into. What this change owns is the length scan.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj oleaut32.lib user32.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c impl.obj oleaut32.lib user32.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
