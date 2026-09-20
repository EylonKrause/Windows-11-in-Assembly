@echo off
REM changes/264-rtlinitutf8string/build.bat: assemble, gate on correctness, then benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
rem  THE IMPLEMENTATION IS CHANGE 095's, ALIASED RATHER THAN COPIED. probes/equiv.c proved
rem  RtlInitUTF8String identical to RtlInitString over 125883 cases -- every ordered byte pair
rem  included -- so pasting 095's assembly here would create a second copy that a future correction
rem  to 095 would silently leave behind. That is exactly how change 132's extension rule ended up
rem  wrong in four landed changes at once. impl.asm is a linker ALIAS; 095's object is linked.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep095.obj "%H%..\095-rtlinitstring\impl.asm" >nul || goto :err
cl /nologo /O2 correctness.c impl.obj dep095.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c impl.obj dep095.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
