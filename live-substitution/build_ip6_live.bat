@echo off
REM ===========================================================================
Rem  live-run proof for changes 166 (RtlIpv6StringToAddressW) and 250
REM  (RtlIpv6StringToAddressExW); the FIRST live proof this family has had.
REM  121, 122 and 166 all landed with correctness, speed and ABI gates and no
REM  hot-patch at all.
REM
REM  The two are proved together because the relationship between them IS change
Rem  250: patching W alone moves the shipped, unpatched ExW onto our core, since
REM  the Ex form reaches the address body by a direct internal call to the very
REM  address the W export names. The harness proves that with a counter.
REM
REM  Sacrificial single-threaded child; no system process is touched, nothing on
REM  disk is modified.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Foip6_w.obj   "%C%\166-rtlipv6stringtoaddressw\impl.asm"      >nul || goto :err
ml64 /nologo /c /Foip6_exw.obj "%C%\250-rtlipv6stringtoaddressexw\impl.asm"    >nul || goto :err
cl /nologo /O2 live_subst_ip6.c ip6_w.obj ip6_exw.obj /Fe:live_subst_ip6.exe >nul || goto :err
"%H%live_subst_ip6.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
