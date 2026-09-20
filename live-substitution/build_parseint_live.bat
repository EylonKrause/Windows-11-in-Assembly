@echo off
REM ===========================================================================
REM  Live substitution for the six ucrtbase integer parsers:
REM    108 atoi  109 _atoi64  110 strtol  111 strtoul  112 _strtoi64  113 _strtoui64
REM
REM  /MD is required: the harness compares errno after every call, and errno must
REM  be the one the PATCHED ucrtbase writes. With the static CRT the test would
REM  read its own copy and every ERANGE comparison would be meaningless.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fopi108.obj "%C%\108-atoi\impl.asm"      >nul || goto :err
ml64 /nologo /c /Fopi109.obj "%C%\109-atoi64\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fopi110.obj "%C%\110-strtol\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fopi111.obj "%C%\111-strtoul\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fopi112.obj "%C%\112-strtoi64\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fopi113.obj "%C%\113-strtoui64\impl.asm" >nul || goto :err

cl /nologo /O2 /MD live_subst_parseint.c pi108.obj pi109.obj pi110.obj pi111.obj pi112.obj pi113.obj ^
   /Fe:live_subst_parseint.exe >nul || goto :err

"%H%live_subst_parseint.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
