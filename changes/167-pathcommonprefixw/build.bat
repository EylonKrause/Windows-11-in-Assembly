@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..
cd /d "%H%"
rem  The case-fold is EXACTLY RtlUpcaseUnicodeChar -- this change's own go/no-go established that,
rem  0 differences over 65534 code-unit pairs against 947 for a plain ASCII fold. Change 210 already
rem  builds that table from the OS once, so it is linked rather than rebuilt here.
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 /c /Fodep210up.obj "%C%\210-comparestringordinal\upcase.c" >nul || goto :err
cl /nologo /O2 /EHa correctness.c reference.c impl.obj dep210up.obj shlwapi.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /Od /I "%H%..\..\harness" bench.c impl.obj dep210up.obj shlwapi.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
