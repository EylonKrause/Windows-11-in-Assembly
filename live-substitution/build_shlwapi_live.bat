@echo off
REM ===========================================================================
Rem  live-run proof for changes 168-176 (shlwapi), 212 (PathFindFileNameA), 213 (StrRChrA), 214-216 (the narrow span family), 217+132 (both PathFindExtension halves), 218 (StrTrimA), 219 (PathStripPathA), 220 (StrChrA), 221 (PathRemoveBlanksA), 222 (PathRemoveExtensionA), 223 (PathUndecorateA), 224 (PathRenameExtensionA), 226 (PathRemoveArgsA), 231 (StrCatBuffA), 232 (PathRemoveBackslashA), 233 (PathQuoteSpacesA), 234 (PathFindNextComponentA), 235 (PathIsFileSpecA), 236 (PathCommonPrefixA), 237 (PathIsPrefixA), 238 (PathMakePrettyA).
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
ml64 /nologo /c /Fosw_pab.obj  "%C%\142-pathaddbackslashw\impl.asm"       >nul || goto :err
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
ml64 /nologo /c /Fosw_trma.obj "%C%\218-strtrima\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_spa.obj "%C%\219-pathstrippatha\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_scha.obj "%C%\220-strchra\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_prba.obj "%C%\221-pathremoveblanksa\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_prxa.obj "%C%\222-pathremoveextensiona\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_puda.obj "%C%\223-pathundecoratea\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_prea.obj "%C%\224-pathrenameextensiona\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_praa.obj "%C%\226-pathremoveargsa\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_scba.obj "%C%\231-strcatbuffa\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_prbsa.obj "%C%\232-pathremovebackslasha\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pqsa.obj "%C%\233-pathquotespacesa\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pfnca.obj "%C%\234-pathfindnextcomponenta\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pifsa.obj "%C%\235-pathisfilespeca\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pcpa.obj "%C%\236-pathcommonprefixa\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pipa.obj "%C%\237-pathisprefixa\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosw_pmpa.obj "%C%\238-pathmakeprettya\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_shlwapi.c sw_pcrb.obj sw_pud.obj sw_pra.obj sw_cpyn.obj sw_chrn.obj sw_catb.obj sw_prb.obj sw_pqs.obj sw_pfnc.obj sw_pffa.obj sw_srca.obj sw_cspa.obj sw_pbka.obj sw_spna.obj sw_pxa.obj sw_pxw.obj sw_trma.obj sw_spa.obj sw_scha.obj sw_prba.obj sw_prxa.obj sw_puda.obj sw_prea.obj sw_praa.obj sw_scba.obj sw_prbsa.obj sw_pqsa.obj sw_pfnca.obj sw_pifsa.obj sw_pcpa.obj sw_pipa.obj sw_pmpa.obj sw_pab.obj /Fe:live_subst_shlwapi.exe >nul || goto :err
"%H%live_subst_shlwapi.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
