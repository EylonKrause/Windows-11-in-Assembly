@echo off
REM ===========================================================================
REM  Live substitution for five ntdll address formatters:
REM    059 RtlIpv4AddressToStringA    060 RtlEthernetAddressToStringA
REM    061 RtlIpv4AddressToStringW    062 RtlEthernetAddressToStringW
REM    066 RtlIpv4AddressToStringExW
REM
REM  dec2.c is linked ONCE: changes 061 and 066 ship the same file (identical
REM  md5) and it defines wia_dec2/wia_dec2_init, so linking both copies is a
REM  duplicate-symbol error rather than a choice.
REM
REM  THE IPv6 FORMATTERS ARE NOT IN THIS HARNESS. Change 063's tables.c and
REM  change 059's dec2b.c both define wia_dec2b, so they cannot share an image;
REM  063/064/068/069 also carry v6core/v6ref objects of their own and want a
REM  harness of their own.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Foaf059.obj "%C%\059-rtlipv4addresstostringa\impl.asm"      >nul || goto :err
ml64 /nologo /c /Foaf060.obj "%C%\060-rtlethernetaddresstostringa\impl.asm"  >nul || goto :err
ml64 /nologo /c /Foaf061.obj "%C%\061-rtlipv4addresstostringw\impl.asm"      >nul || goto :err
ml64 /nologo /c /Foaf062.obj "%C%\062-rtlethernetaddresstostringw\impl.asm"  >nul || goto :err
ml64 /nologo /c /Foaf066.obj "%C%\066-rtlipv4addresstostringexw\impl.asm"    >nul || goto :err

cl /nologo /O2 /c /Foafd2b.obj "%C%\059-rtlipv4addresstostringa\dec2b.c"     >nul || goto :err
cl /nologo /O2 /c /Foafh2u.obj "%C%\060-rtlethernetaddresstostringa\hex2u.c" >nul || goto :err
cl /nologo /O2 /c /Foafd2.obj  "%C%\061-rtlipv4addresstostringw\dec2.c"      >nul || goto :err
cl /nologo /O2 /c /Foafh2w.obj "%C%\062-rtlethernetaddresstostringw\hex2uw.c" >nul || goto :err

cl /nologo /O2 live_subst_addrfmt.c af059.obj af060.obj af061.obj af062.obj af066.obj ^
   afd2b.obj afh2u.obj afd2.obj afh2w.obj /Fe:live_subst_addrfmt.exe >nul || goto :err

"%H%live_subst_addrfmt.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
