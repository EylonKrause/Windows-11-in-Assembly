@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..
cd /d "%H%"
REM  This change IS a wrapper around change 243, so 243's assembly and 243's oracle are COMPILED
REM  ALONGSIDE rather than copied -- the same arrangement change 242 uses for the same reason.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fopccx243.obj "%C%\243-pathcchcanonicalizeex\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Foref243.obj "%C%\243-pathcchcanonicalizeex\reference.c" >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj pccx243.obj ref243.obj shlwapi.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /Od /I "%H%..\..\harness" bench.c impl.obj pccx243.obj shlwapi.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
