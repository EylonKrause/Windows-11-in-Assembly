@echo off
REM ===========================================================================
REM  Live substitution for the eight ntdll string-to-address parsers:
REM    115 RtlIpv4StringToAddressW      116 RtlIpv4StringToAddressExA
REM    117 RtlIpv4StringToAddressExW    118 RtlGUIDFromString
REM    119 RtlEthernetStringToAddressA  120 RtlEthernetStringToAddressW
REM    121 RtlIpv6StringToAddressA      122 RtlIpv6StringToAddressExA
REM  None of them needs a helper .c.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fopa115.obj "%C%\115-rtlipv4stringtoaddressw\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fopa116.obj "%C%\116-rtlipv4stringtoaddressex\impl.asm"      >nul || goto :err
ml64 /nologo /c /Fopa117.obj "%C%\117-rtlipv4stringtoaddressexw\impl.asm"     >nul || goto :err
ml64 /nologo /c /Fopa118.obj "%C%\118-rtlguidfromstring\impl.asm"             >nul || goto :err
ml64 /nologo /c /Fopa119.obj "%C%\119-rtlethernetstringtoaddress\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fopa120.obj "%C%\120-rtlethernetstringtoaddressw\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fopa121.obj "%C%\121-rtlipv6stringtoaddress\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fopa122.obj "%C%\122-rtlipv6stringtoaddressex\impl.asm"      >nul || goto :err

cl /nologo /O2 live_subst_parseaddr.c pa115.obj pa116.obj pa117.obj pa118.obj ^
   pa119.obj pa120.obj pa121.obj pa122.obj /Fe:live_subst_parseaddr.exe >nul || goto :err

"%H%live_subst_parseaddr.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
