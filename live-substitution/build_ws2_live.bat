@echo off
REM ===========================================================================
Rem  ws2_32's IP-conversion exports are already covered by landed ntdll changes.
REM  inet_addr and inet_ntop do not parse or format an address at all -- they
REM  dispatch through their import table into ntdll!RtlIpv4StringToAddressA and
REM  ntdll!RtlIpv4AddressToStringExA, which are changes 114 and 065. This patches
REM  those ntdll exports and proves, with a counter, that the ws2_32 names route
REM  through them -- no new implementation, no new contract.
REM  Sacrificial single-threaded child; no system process touched, nothing on disk.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fows2_p2a.obj  "%C%\114-rtlipv4stringtoaddress\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fows2_a2sx.obj "%C%\065-rtlipv4addresstostringexa\impl.asm" >nul || goto :err
REM  change 065 keeps its decimal-to-bytes helper in a companion source, so it is compiled too.
cl /nologo /O2 live_subst_ws2.c "%C%\065-rtlipv4addresstostringexa\dec2b.c" ws2_p2a.obj ws2_a2sx.obj ws2_32.lib /Fe:live_subst_ws2.exe >nul || goto :err
"%H%live_subst_ws2.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
