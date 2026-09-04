@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\064-rtlipv6addresstostringw
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fov6corew.obj "%C%\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Fotablesw.obj "%C%\tables.c" >nul || goto :err
cl /nologo /O2 /c /Fov6refw.obj "%C%\reference.c" >nul || goto :err
cl /nologo /O2 correctness.c reference.c v6refw.obj tablesw.obj impl.obj v6corew.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /Od /I "%H%..\..\harness" bench.c reference.c v6refw.obj tablesw.obj impl.obj v6corew.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
