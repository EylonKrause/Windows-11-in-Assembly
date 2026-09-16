@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..
cd /d "%H%"
rem  THE WALK IS CHANGE 167's, and that is the whole change: this function was parked as
rem  "we derived it, and it is PathCommonPrefixW, which is parked". 167 has landed, so 167's
rem  assembly is linked alongside -- together with change 210's upcase table, which 167 folds with.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep167.obj "%C%\167-pathcommonprefixw\impl.asm" >nul || goto :err
rem  and change 001 for the prefix's length -- the benchmark's "differ at 8" row is what insisted:
rem  a scalar wcslen over a prefix the walk had already abandoned WAS the whole function there.
ml64 /nologo /c /Fodep001.obj "%C%\001-wcslen\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Fodep210up.obj "%C%\210-comparestringordinal\upcase.c" >nul || goto :err
cl /nologo /O2 /EHa correctness.c reference.c impl.obj dep167.obj dep001.obj dep210up.obj shlwapi.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /Od /I "%H%..\..\harness" bench.c impl.obj dep167.obj dep001.obj dep210up.obj shlwapi.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
