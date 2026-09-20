@echo off
REM ===========================================================================
Rem  2ND PC live-run proof -- amd Ryzen 9 8940HX (Zen 4), Win11 26200.9445
REM  Builds the five 2nd-PC variants (impl_2ndpc.asm) and hot-patches the real
REM  ucrtbase / ntdll / shlwapi exports in THIS process's own copy-on-write copy,
REM  proving Windows executes our assembly, then reverts.
REM  Sacrificial single-threaded child; no system process is ever touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fop2_lwr.obj  "%C%\047-strlwr\impl_2ndpc.asm"               >nul || goto :err
ml64 /nologo /c /Fop2_itoc.obj "%C%\097-rtlintegertochar\impl_2ndpc.asm"     >nul || goto :err
ml64 /nologo /c /Fop2_cmp.obj  "%C%\008-rtlcompareunicodestring\impl_2ndpc.asm" >nul || goto :err
ml64 /nologo /c /Fop2_spn.obj  "%C%\135-strspnw\impl_2ndpc.asm"              >nul || goto :err
ml64 /nologo /c /Fop2_prb.obj  "%C%\141-pathremoveblanksw\impl_2ndpc.asm"    >nul || goto :err
cl /nologo /O2 /c /Fop2_upcase.obj "%C%\008-rtlcompareunicodestring\upcase.c" >nul || goto :err
cl /nologo /O2 live_subst_2ndpc.c p2_lwr.obj p2_itoc.obj p2_cmp.obj p2_spn.obj p2_prb.obj p2_upcase.obj /Fe:live_subst_2ndpc.exe >nul || goto :err
"%H%live_subst_2ndpc.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
