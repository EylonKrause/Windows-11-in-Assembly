@echo off
REM ===========================================================================
REM  Live substitution for the three ntdll time conversions and the GUID
REM  formatter:
REM    126 RtlTimeToTimeFields       127 RtlTimeFieldsToTime
REM    128 RtlSecondsSince1970ToTime 058 RtlStringFromGUIDEx
REM  058 is the only one with a helper (hex2.c).
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fotm126.obj "%C%\126-rtltimetotimefields\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fotm127.obj "%C%\127-rtltimefieldstotime\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fotm128.obj "%C%\128-rtlsecondssince1970totime\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fotm058.obj "%C%\058-rtlstringfromguidex\impl.asm"        >nul || goto :err
cl /nologo /O2 /c /Fotmhex.obj "%C%\058-rtlstringfromguidex\hex2.c"        >nul || goto :err

cl /nologo /O2 live_subst_time.c tm126.obj tm127.obj tm128.obj tm058.obj tmhex.obj ^
   /Fe:live_subst_time.exe >nul || goto :err

"%H%live_subst_time.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
