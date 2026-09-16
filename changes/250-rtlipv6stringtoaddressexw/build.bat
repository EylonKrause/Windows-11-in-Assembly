@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..
cd /d "%H%"
rem  AN ENVELOPE OVER CHANGE 166, and the build script says so rather than hiding it. The shipped
rem  ExW calls RtlIpv6StringToAddressW's own RVA (0x0C318E -> 0x0C33F0), so this links 166's
rem  assembly alongside instead of re-deriving an eight-group hex grammar with "::" compression, an
rem  embedded IPv4 tail and seventeen Unicode digit blocks. Same arrangement 246 has with 243, 247
rem  with 132, and 249 with 225 and 244.
ml64 /nologo /c impl.asm >nul || goto :err
ml64 /nologo /c /Fodep166.obj "%C%\166-rtlipv6stringtoaddressw\impl.asm" >nul || goto :err
cl /nologo /O2 correctness.c reference.c impl.obj dep166.obj /Fe:correctness.exe >nul || goto :err
"%H%correctness.exe" || (echo CORRECTNESS FAILED & goto :err)
cl /nologo /Od /I "%H%..\..\harness" bench.c impl.obj dep166.obj /Fe:bench.exe >nul || goto :err
"%H%bench.exe"
endlocal & exit /b 0
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
