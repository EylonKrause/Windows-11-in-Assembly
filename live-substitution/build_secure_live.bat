@echo off
REM ===========================================================================
REM  Live substitution for the eight ucrtbase bounds-checked string functions:
REM    150 strcpy_s  151 wcscpy_s  152 strcat_s  153 wcscat_s
REM    154 strncpy_s 155 wcsncpy_s 156 strncat_s 157 wcsncat_s
REM
Rem  /md is required here, not merely preferred: the harness installs an
REM  invalid-parameter handler with _set_invalid_parameter_handler, and that
REM  handler must be the one the PATCHED ucrtbase exports consult. With the
REM  static CRT the test would set a handler in its own copy and the export
REM  would still terminate the process on the first NULL argument.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fosec150.obj "%C%\150-strcpy-s\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fosec151.obj "%C%\151-wcscpy-s\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fosec152.obj "%C%\152-strcat-s\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fosec153.obj "%C%\153-wcscat-s\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fosec154.obj "%C%\154-strncpy-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fosec155.obj "%C%\155-wcsncpy-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fosec156.obj "%C%\156-strncat-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fosec157.obj "%C%\157-wcsncat-s\impl.asm"  >nul || goto :err

cl /nologo /O2 /MD live_subst_secure.c sec150.obj sec151.obj sec152.obj sec153.obj ^
   sec154.obj sec155.obj sec156.obj sec157.obj /Fe:live_subst_secure.exe >nul || goto :err

"%H%live_subst_secure.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
