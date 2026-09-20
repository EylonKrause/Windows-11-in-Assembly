@echo off
REM ===========================================================================
REM  Live substitution for the four ntdll IPv6 formatters:
REM    063 RtlIpv6AddressToStringA    064 RtlIpv6AddressToStringW
REM    068 RtlIpv6AddressToStringExA  069 RtlIpv6AddressToStringExW
REM
REM  068 and 069 build on 063's and 064's implementations as their core (their
REM  own build.bat assembles them as v6core.obj / v6corew.obj), so all four are
REM  linked here from their own directories and the two tables.c files, whose
REM  symbols are distinct, come from 063 and 064.
REM
Rem  separate from build_addrfmt_live.bat for a link reason: 063's tables.c and
REM  059's dec2b.c both define wia_dec2b, so the IPv4 and IPv6 groups cannot
REM  share an image.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fov6a.obj  "%C%\063-rtlipv6addresstostringa\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fov6w.obj  "%C%\064-rtlipv6addresstostringw\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fov6ea.obj "%C%\068-rtlipv6addresstostringexa\impl.asm" >nul || goto :err
ml64 /nologo /c /Fov6ew.obj "%C%\069-rtlipv6addresstostringexw\impl.asm" >nul || goto :err

cl /nologo /O2 /c /Fov6ta.obj "%C%\063-rtlipv6addresstostringa\tables.c" >nul || goto :err
cl /nologo /O2 /c /Fov6tw.obj "%C%\064-rtlipv6addresstostringw\tables.c" >nul || goto :err

cl /nologo /O2 live_subst_v6fmt.c v6a.obj v6w.obj v6ea.obj v6ew.obj v6ta.obj v6tw.obj ^
   /Fe:live_subst_v6fmt.exe >nul || goto :err

"%H%live_subst_v6fmt.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
