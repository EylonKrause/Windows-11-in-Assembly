@echo off
REM ===========================================================================
REM  Live substitution for the three fan-in-driven conversions:
REM    289 kernelbase!WideCharToMultiByte, 290 kernelbase!MultiByteToWideChar,
REM    291 kernel32!ExpandEnvironmentStringsW
REM
REM  /MD because 289 and 291 reference imports the static CRT would resolve
Rem  differently, and because the harness compares the last error value, which
REM  needs the same CRT/Win32 pairing the change itself was gated under.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fo cvt_wc2mb.obj  "%C%\289-widechartomultibyte\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fo cvt_mbtwc.obj  "%C%\290-multibytetowidechar\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fo cvt_expand.obj "%C%\291-expandenvironmentstringsw\impl.asm"  >nul || goto :err

cl /nologo /O2 /MD /c live_subst_cvt.c /Folsc.obj >nul || goto :err
link /nologo /OUT:live_subst_cvt.exe lsc.obj cvt_wc2mb.obj cvt_mbtwc.obj cvt_expand.obj ^
     kernel32.lib >nul || goto :err

"%H%live_subst_cvt.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
