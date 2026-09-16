@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..
cd /d "%H%"
rem  THIS CHANGE IS A COMPOSITION, and the build script says so rather than hiding it. The envelope
rem  is six instructions; the length comes from change 225 (INCLUDING its SEH wrapper, which is what
rem  makes a faulting URL return S_OK with the identity seed instead of crashing) and the hash from
rem  change 244. Both are assembled and linked here rather than re-derived, the same arrangement 246
rem  has with 243 and 247 with 132.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep225.obj "%C%\225-lstrlena\impl.asm" >nul || goto :err
ml64 /nologo /c /Fodep244.obj "%C%\244-hashdata\impl.asm" >nul || goto :err
cl /nologo /O2 /EHa /c /Fodep225seh.obj "%C%\225-lstrlena\seh.c" >nul || goto :err
cl /nologo /O2 /EHa correctness.c reference.c impl.obj dep225.obj dep225seh.obj dep244.obj shlwapi.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /Od /I "%H%..\..\harness" bench.c impl.obj dep225.obj dep225seh.obj dep244.obj shlwapi.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
