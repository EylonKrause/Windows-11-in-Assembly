@echo off
REM ===========================================================================
REM  Live substitution for the last seven uncovered landed changes, four DLLs:
REM    ntdll       076 RtlCrc64   100 RtlLargeIntegerToChar
REM                295 RtlUnicodeStringToInteger
REM    kernelbase  288 FoldStringW (MAP_FOLDDIGITS)
REM    kernel32    292 FileTimeToSystemTime   293 SystemTimeToFileTime
REM    combase     297 WindowsCompareStringOrdinal
REM  297 calls RoOriginateErrorW, so runtimeobject.lib is needed.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fol7076.obj "%C%\076-rtlcrc64\impl.asm"                    >nul || goto :err
ml64 /nologo /c /Fol7100.obj "%C%\100-rtllargeintegertochar\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fol7295.obj "%C%\295-rtlunicodestringtointeger\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fol7288.obj "%C%\288-foldstringw-digits\impl.asm"          >nul || goto :err
ml64 /nologo /c /Fol7292.obj "%C%\292-filetimetosystemtime\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fol7293.obj "%C%\293-systemtimetofiletime\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fol7297.obj "%C%\297-windowscomparestringordinal\impl.asm" >nul || goto :err

cl /nologo /O2 /c /Fol7crc.obj "%C%\076-rtlcrc64\crctab.c"                  >nul || goto :err
cl /nologo /O2 /c /Fol7tab.obj "%C%\288-foldstringw-digits\tables.c"        >nul || goto :err

cl /nologo /O2 live_subst_last7.c l7076.obj l7100.obj l7295.obj l7288.obj ^
   l7292.obj l7293.obj l7297.obj l7crc.obj l7tab.obj ^
   ntdll.lib runtimeobject.lib /Fe:live_subst_last7.exe >nul || goto :err

"%H%live_subst_last7.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
