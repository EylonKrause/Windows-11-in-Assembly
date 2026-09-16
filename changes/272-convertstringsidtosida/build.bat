@echo off
REM changes/272-convertstringsidtosida/build.bat -- assemble, gate on correctness, benchmark.
REM
REM  CHANGE 269 IS LINKED, NOT COPIED. probes/codepage.c established that ConvertStringSidToSidA(s)
REM  is exactly ConvertStringSidToSidW(MultiByteToWideChar(CP_ACP, 0, s, -1, ...)) over every byte in
REM  every field, every printable ASCII pair against the alias table, and the sequences that do not
REM  translate -- so the parser is 269's, and this change is the widening. 269's two OS-derived
REM  tables come with it.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set D=%H%..\269-convertstringsidtosid
cd /d "%H%"
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep269.obj "%D%\impl.asm" >nul || goto :err
rem  Both changes have a reference.c, so 269's is compiled separately into a named object.
cl /nologo /O2 /c /Fodep269ref.obj "%D%\reference.c" >nul || goto :err
cl /nologo /O2 /c /Fodep269al.obj  "%D%\aliases.c"  >nul || goto :err
cl /nologo /O2 /c /Fodep269cl.obj  "%D%\classify.c" >nul || goto :err
cl /nologo /O2 correctness.c reference.c widen.c impl.obj dep269.obj dep269ref.obj dep269al.obj dep269cl.obj advapi32.lib user32.lib /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED - not benchmarking & goto :err)
cl /nologo /O2 /I "%H%..\..\harness" bench.c reference.c widen.c impl.obj dep269.obj dep269ref.obj dep269al.obj dep269cl.obj advapi32.lib user32.lib /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
