@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..
cd /d "%H%"
REM  The append point is change 132's rule, so 132's assembly is assembled and linked alongside --
REM  the same arrangement 246 uses over 243, and for the same reason: that rule is the one part of
Rem  this function subtle enough to get wrong, and 132 already shipped wrong on it once.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fopfe132.obj "%C%\132-pathfindextensionw\impl.asm" >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj pfe132.obj shlwapi.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /Od /I "%H%..\..\harness" bench.c impl.obj pfe132.obj shlwapi.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
