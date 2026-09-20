@echo off
REM ===========================================================================
REM  Live substitution for the four ucrtbase integer formatters:
REM    054 _ultoa  055 _ui64toa  056 _itoa  057 _i64toa
REM
REM  dec2b.c is linked ONCE: all four changes ship the identical file (same md5)
REM  and it defines wia_dec2b/wia_dec2b_init, so linking more than one copy is a
REM  duplicate-symbol error rather than a choice.
REM
REM  /MD so the process uses the SAME ucrtbase this harness patches.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Foit054.obj "%C%\054-ultoa\impl.asm"   >nul || goto :err
ml64 /nologo /c /Foit055.obj "%C%\055-ui64toa\impl.asm" >nul || goto :err
ml64 /nologo /c /Foit056.obj "%C%\056-itoa\impl.asm"    >nul || goto :err
ml64 /nologo /c /Foit057.obj "%C%\057-i64toa\impl.asm"  >nul || goto :err
cl /nologo /O2 /c /Foitd2b.obj "%C%\054-ultoa\dec2b.c"  >nul || goto :err

cl /nologo /O2 /MD live_subst_itoa.c it054.obj it055.obj it056.obj it057.obj itd2b.obj ^
   /Fe:live_subst_itoa.exe >nul || goto :err

"%H%live_subst_itoa.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
