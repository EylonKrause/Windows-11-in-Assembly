@echo off
REM ===========================================================================
Rem  live-run proof for change 281 (shlwapi!StrChrIW).
REM
Rem  The shipped export costs 43 ns per character because its equality test is
REM  CompareStringW with NORM_IGNORECASE, evaluated once per code unit. The
REM  relation it computes is locale-invariant, decided one character at a time,
Rem  symmetric but not transitive -- U+D7B0 matches U+D7A2 and U+D7B1 matches
REM  U+D7A2, but U+D7B0 does not match U+D7B1, so it has no classes and is
REM  stored per NEEDLE.
REM
REM  All four dispatch shapes are driven explicitly rather than by a uniform
REM  draw: the bitmap needles are 3320 of 65535 and a short random corpus could
REM  miss them entirely. Every 32-byte start alignment is driven too, because the
REM  implementation aligns DOWN and masks off what precedes the string.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of shlwapi. No system process is touched, nothing on disk is modified, and
REM  the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes\281-strchriw
cd /d "%H%"
ml64 /nologo /c /Fostrchriw.obj "%C%\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_strchriw.c strchriw.obj "%C%\tables.c" "%C%\foldsets.c" "%C%\foldbig.c" "%C%\foldnul.c" /Fe:live_subst_strchriw.exe >nul || goto :err
"%H%live_subst_strchriw.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
