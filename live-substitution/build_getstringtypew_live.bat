@echo off
REM ===========================================================================
Rem  live-run proof for change 287 (kernelbase!GetStringTypeW).
REM
REM  The shipped export costs 421.05 ns for 511 code units, measured by
REM  discovery/uncovered_2026b.c, the most expensive uncovered export in that
REM  sweep that is not already a known collation wall.
REM
REM  That a TABLE can reproduce it at all was measured, not assumed:
REM  probes/contract.c found the classification CONTEXT-FREE (20000 random strings
REM  up to 2048 units, 0 words differing from the class the same character gets
REM  alone, all three info types) and LOCALE-INVARIANT (the whole CT_CTYPE1 table
REM  rebuilt under seven thread locales, 0 entries different). Either failing would
REM  have killed the change the way collation killed changes 274 and 276.
REM
Rem  one hazard is specific to this change: our tables are derived from the live
REM  export, so the harness builds and asserts them BEFORE installing the patch.
REM  Deriving them through our own replacement would make the gate compare us
REM  against ourselves, which is the most comfortable way to pass and proves
REM  nothing.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of kernelbase. No system process is touched, nothing on disk is modified, and
REM  the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes\287-getstringtypew
cd /d "%H%"
ml64 /nologo /c /Fogetstringtypew.obj "%C%\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_getstringtypew.c getstringtypew.obj "%C%\tables.c" /Fe:live_subst_getstringtypew.exe >nul || goto :err
"%H%live_subst_getstringtypew.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
