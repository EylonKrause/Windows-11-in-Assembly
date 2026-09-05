@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
rem bench.c built /Od so MSVC can't hoist the pure-ish call out of the timing loop.
cl /nologo /Od /I "%H%..\..\harness" bench.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
