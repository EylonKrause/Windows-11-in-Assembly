@echo off
REM ===========================================================================
Rem  live-run proof for change 284 (shlwapi!StrStrIW).
REM
REM  The shipped export costs 1281 ns over 511 code units. Its comparison is PER
REM  CHARACTER, not a collation over spans -- "ab<SOFT HYPHEN>cd" does not contain
REM  "abc" -- which is what makes this change writable on change 281's relation.
REM
Rem  nothing was inherited from change 283. probes/contract.c re-measured every
REM  question, because 283 shipped two wrong drafts and both passed a gate. The
REM  rule: the string behaves as though the terminator were followed by ENDLESS
REM  NULs, and those NULs are never loaded -- with 'W' after the terminator,
Rem  {q,shy,shy} is found and {q,w} is not. So a needle whose tail matches a NUL
REM  can match past the end, and a needle longer than the whole string can match.
REM  A match may START only at a real character, an embedded NUL ends the search,
REM  and an EMPTY needle returns NULL -- the opposite of C strstr.
REM
Rem  Both filter paths are driven, and so is the wide threshold itself: U+00AD
REM  reaches the wide path through a 255 sentinel, which leaves the real
REM  threshold untested, so U+004B (five partners) is forced separately and
REM  matched through the highest member of its set.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of shlwapi. No system process is touched, nothing on disk is modified, and
REM  the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes\284-strstriw
set S=%H%..\changes\281-strchriw
cd /d "%H%"
ml64 /nologo /c /Fostrstriw.obj "%C%\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_strstriw.c strstriw.obj "%S%\tables.c" "%S%\foldsets.c" "%S%\foldbig.c" "%S%\foldnul.c" /Fe:live_subst_strstriw.exe >nul || goto :err
"%H%live_subst_strstriw.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
