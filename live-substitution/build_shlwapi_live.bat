@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for changes 168-176 (shlwapi), 212 (PathFindFileNameA), 213 (StrRChrA), 214-216 (the narrow span family), 217+132 (BOTH PathFindExtension halves).
REM  Hot-patches the real shlwapi exports in THIS process's own copy-on-write
REM  copy, proving Windows executes our assembly, then reverts and VERIFIES the
REM  restore byte-for-byte. Sacrificial single-threaded child; no system process
REM  is ever touched, and nothing on disk is modified.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fosw_cpyn.obj "%C%\168-strcpynw\impl.asm"                >nul || goto :err
ml64 /nologo /c /Fosw_chrn.obj "%C%\169-strchrnw\impl.asm"                >nul || goto :err
ml64 /nologo /c /Fosw_catb.obj "%C%\170-strcatbuffw\impl.asm"             >nul || goto :err
ml64 /nologo /c /Fosw_prb.obj  "%C%\171-pathremovebackslashw\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fosw_pqs.obj  "%C%\172-pathquotespacesw\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fosw_pfnc.obj "%C%\173-pathfindnextcomponentw\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fosw_pcrb.obj "%C%\176-pathcchremovebackslash\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pud.obj  "%C%\174-pathundecoratew\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fosw_pra.obj  "%C%\175-pathremoveargsw\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fosw_pffa.obj "%C%\212-pathfindfilenamea\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_srca.obj "%C%\213-strrchra\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_cspa.obj "%C%\214-strcspna\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pbka.obj "%C%\215-strpbrka\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_spna.obj "%C%\216-strspna\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pxa.obj "%C%\217-pathfindextensiona\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pxw.obj "%C%\132-pathfindextensionw\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_shlwapi.c sw_pcrb.obj sw_pud.obj sw_pra.obj sw_cpyn.obj sw_chrn.obj sw_catb.obj sw_prb.obj sw_pqs.obj sw_pfnc.obj sw_pffa.obj sw_srca.obj sw_cspa.obj sw_pbka.obj sw_spna.obj sw_pxa.obj sw_pxw.obj /Fe:live_subst_shlwapi.exe >nul || goto :err
"%H%live_subst_shlwapi.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
