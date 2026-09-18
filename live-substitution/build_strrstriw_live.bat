@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 283 (shlwapi!StrRStrIW).
REM
REM  The shipped export costs 21816.97 ns over 511 code units. Its comparison is
REM  PER CHARACTER, not a collation over spans -- "ab<SOFT HYPHEN>cd" does not
REM  contain "abc" -- which is what makes this change writable on change 281's
REM  relation.
REM
REM  Its shape is NOT StrRChrIW's: `end` bounds only where a match may START, the
REM  haystack is NUL-TERMINATED, and the terminator beats `end`. With no
REM  terminator the export FAULTS, so every haystack here is terminated and the
REM  guard-page cases put that terminator as the last readable code unit.
REM
REM  Both filter paths are driven: the vector filter on the needle's first
REM  character, and the WIDE path taken when that character has more than four
REM  partners, which bypasses the filter and verifies every position.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of shlwapi. No system process is touched, nothing on disk is modified, and
REM  the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes\283-strrstriw
set S=%H%..\changes\281-strchriw
cd /d "%H%"
ml64 /nologo /c /Fostrrstriw.obj "%C%\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_strrstriw.c strrstriw.obj "%S%\tables.c" "%S%\foldsets.c" "%S%\foldbig.c" "%S%\foldnul.c" /Fe:live_subst_strrstriw.exe >nul || goto :err
"%H%live_subst_strrstriw.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
