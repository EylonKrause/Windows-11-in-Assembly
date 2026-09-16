@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
rem seh.c holds the envelope: the INPLACE-before-validation ordering, the AS_UTF8 refusal, the __try
rem that reproduces lstrlenA's swallow of an access violation, and the staging buffer for the one
rem overlap direction a single forward pass cannot do. /O2 on it, because it IS the fast path.
cl /nologo /O2 /c seh.c >nul || goto :err
cl /nologo /O2 correctness.c reference.c seh.obj impl.obj shlwapi.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /Od /I "%H%..\..\harness" bench.c seh.obj impl.obj shlwapi.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
