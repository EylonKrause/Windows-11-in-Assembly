@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
rem bench.c is built /Od on purpose: wia_wcspbrk is a pure, side-effect-free function
rem with loop-invariant args, so at /O2 MSVC devirtualizes the harness call and hoists
rem it clean out of the timing loop (bogus "0.00 ns"). /Od forces a real call each
rem iteration for BOTH our fn and the system fn; the identical loop overhead only
rem UNDERSTATES our win, so the LANDS gate stays honest. (impl.asm is native asm; the
rem reference/harness overhead is symmetric.) See RESULTS.md for rdtscp cross-checks.
cl /nologo /Od /I "%H%..\..\harness" bench.c reference.c impl.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
