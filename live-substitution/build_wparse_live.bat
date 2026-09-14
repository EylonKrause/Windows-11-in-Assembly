@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for changes 186-191 -- the WIDE integer parser family:
REM  _wtoi/_wtol, _wtoi64, wcstol, wcstoul, _wcstoi64/wcstoll, _wcstoui64/wcstoull.
REM  Six implementations, EIGHT exported names.
REM  /MD is REQUIRED: errno and the invalid-parameter handler must be UCRTBASE's --
REM  the same ones our assembly writes through via its exported _errno /
REM  _invalid_parameter_noinfo. With the static CRT the comparison is meaningless.
REM  Sacrificial single-threaded child; no system process is touched, nothing on
REM  disk is modified.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fowp_i.obj    "%C%\186-wtoi\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fowp_i64.obj  "%C%\187-wtoi64\impl.asm"     >nul || goto :err
ml64 /nologo /c /Fowp_l.obj    "%C%\188-wcstol\impl.asm"     >nul || goto :err
ml64 /nologo /c /Fowp_ul.obj   "%C%\189-wcstoul\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fowp_l64.obj  "%C%\190-wcstoi64\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fowp_ul64.obj "%C%\191-wcstoui64\impl.asm"  >nul || goto :err
cl /nologo /O2 /MD live_subst_wparse.c wp_i.obj wp_i64.obj wp_l.obj wp_ul.obj wp_l64.obj wp_ul64.obj /Fe:live_subst_wparse.exe >nul || goto :err
"%H%live_subst_wparse.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
