@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
rem bench.c is built /Od on purpose: wia_strstr is pure and side-effect-free with
rem loop-invariant args, so at /O2 MSVC hoists the call clean out of the timing loop
rem (bogus "0.00 ns"). /Od forces a real call each iteration for BOTH our fn and the
rem system fn; the identical loop overhead only UNDERSTATES our win, so the LANDS gate
rem stays honest.
cl /nologo /Od /I "%H%..\..\harness" bench.c reference.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
