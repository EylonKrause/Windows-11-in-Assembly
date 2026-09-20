@echo off
REM ===========================================================================
Rem  live-run proof for change 286 (shlwapi!StrChrNIW).
REM
Rem  the prototype had to be settled before anything else. Two sources disagreed:
REM  the documented (start, match, count) and discovery/charclass_strcmp_2026.c's
REM  (start, start+511, char), which it labelled "range form" after reusing
REM  StrRChrIW's typedef. probes/contract.c called the same address through both --
REM  the count reading returns the right pointer, the range reading returns NULL --
REM  so the sweep's 1655 ns figure measured a different question, and this change's
REM  bench found the real cost to be 16939 ns over 511 code units.
REM
REM  The count is the number of characters EXAMINED, indices 0..cchMax-1. The
Rem  relation is change 281's. And the terminator stops the scan and is never a
REM  MATCH, which is where this export parts company with changes 283 and 284:
REM  there a needle character matching a NUL matched the terminator itself.
REM
REM  On an UNTERMINATED string the export faults even when the count covers the
REM  buffer, so every string here is terminated; the guard-page cases put that
REM  terminator as the last readable code unit.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of shlwapi. No system process is touched, nothing on disk is modified, and
REM  the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes\286-strchrniw
set S=%H%..\changes\281-strchriw
cd /d "%H%"
ml64 /nologo /c /Fostrchrniw.obj "%C%\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_strchrniw.c strchrniw.obj "%S%\tables.c" "%S%\foldsets.c" "%S%\foldbig.c" "%S%\foldnul.c" /Fe:live_subst_strchrniw.exe >nul || goto :err
"%H%live_subst_strchrniw.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
