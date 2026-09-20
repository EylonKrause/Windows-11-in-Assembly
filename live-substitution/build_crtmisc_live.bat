@echo off
REM ===========================================================================
REM  Live substitution for the seven remaining uncovered ucrtbase routines:
REM    048 _strupr   050 _wcsupr   145 _swab     146 _memccpy
REM    147 strtok_s  148 wcstok_s  149 wcsrchr
REM  All seven are self-contained impl.asm, no helper .c, no init call.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Focm048.obj "%C%\048-strupr\impl.asm"    >nul || goto :err
ml64 /nologo /c /Focm050.obj "%C%\050-wcsupr\impl.asm"    >nul || goto :err
ml64 /nologo /c /Focm145.obj "%C%\145-swab\impl.asm"      >nul || goto :err
ml64 /nologo /c /Focm146.obj "%C%\146-memccpy\impl.asm"   >nul || goto :err
ml64 /nologo /c /Focm147.obj "%C%\147-strtok-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Focm148.obj "%C%\148-wcstok-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Focm149.obj "%C%\149-wcsrchr\impl.asm"   >nul || goto :err

cl /nologo /O2 live_subst_crtmisc.c cm048.obj cm050.obj cm145.obj cm146.obj ^
   cm147.obj cm148.obj cm149.obj /Fe:live_subst_crtmisc.exe >nul || goto :err

"%H%live_subst_crtmisc.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
