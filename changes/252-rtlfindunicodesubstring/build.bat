@echo off
REM changes/252-rtlfindunicodesubstring/build.bat -- assemble, gate on correctness, then benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..
cd /d "%H%"
rem  The case fold is EXACTLY RtlUpcaseUnicodeChar -- probes/gonogo.c established that over all
rem  65535 code units, 0 disagreements, and all 973 units whose upcase differs match their partner.
rem  Change 210 already builds that table from the OS once, so it is linked rather than rebuilt;
rem  casemate.c DERIVES the case-partner table from it, which is what the vector filter compares
rem  against. probes/classsize.c is why that is exact: no case class has more than two members.
ml64 /nologo /c impl.asm >nul || goto :err
cl /nologo /O2 /c /Fodep210up.obj "%C%\210-comparestringordinal\upcase.c" >nul || goto :err
cl /nologo /O2 /c casemate.c >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj casemate.obj dep210up.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj casemate.obj dep210up.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
