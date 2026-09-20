@echo off
REM ===========================================================================
Rem  live-run proof for change 282 (shlwapi!StrRChrIW).
REM
Rem  The shipped export costs 47 ns per character -- the same per-character
REM  collation call change 281 found in StrChrIW, and it scans FORWARD even
REM  though it returns the LAST match, so a hit at the end of the range costs it
REM  as much as a miss.
REM
Rem  This export has no terminator: probes/bounds.c measured that its end pointer
REM  is taken literally and that an end past a guard page FAULTS. So the corpus
REM  plants NULs inside ranges on purpose and pins one case in three to a
REM  PAGE_NOACCESS page, alternating which END of the range touches it --
REM  reading past `end` and reading before `start` are two different mistakes.
REM
REM  Half the ranges are short, so the single-block case, where both edge masks
REM  apply at once, is reached in bulk rather than by luck.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of shlwapi. No system process is touched, nothing on disk is modified, and
REM  the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes\282-strrchriw
set S=%H%..\changes\281-strchriw
cd /d "%H%"
ml64 /nologo /c /Fostrrchriw.obj "%C%\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_strrchriw.c strrchriw.obj "%S%\tables.c" "%S%\foldsets.c" "%S%\foldbig.c" "%S%\foldnul.c" /Fe:live_subst_strrchriw.exe >nul || goto :err
"%H%live_subst_strrchriw.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
