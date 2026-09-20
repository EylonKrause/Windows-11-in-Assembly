@echo off
REM changes/268-rtlunicodestringtoutf8string/build.bat: assemble, gate on correctness, benchmark.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
cd /d "%H%"
rem  THIS CHANGE LINKS CHANGES 016 AND 034. It is a wrapper, and the thing it wraps is the N-form
rem  this project already converted -- RtlUnicodeToUTF8N and RtlUTF8ToUnicodeN. Pasting either of
rem  them here would create a second copy that a future correction would silently leave behind,
rem  which is how change 132's extension rule ended up wrong in four landed changes at once.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep016.obj "%H%..\016-rtlunicodetoutf8n\impl.asm" >nul || goto :err
ml64 /nologo /c /Fodep034.obj "%H%..\034-rtlutf8tounicoden\impl.asm" >nul || goto :err
rem
rem  THE CORRECTNESS GATE IS BUILT OPTIMISED, /O2, DELIBERATELY. Change 257 shipped a draft that
rem  passed this gate at /Od and died with an access violation at /O2.
cl /nologo /O2 correctness.c heapalloc.c impl.obj dep016.obj dep034.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c heapalloc.c impl.obj dep016.obj dep034.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
