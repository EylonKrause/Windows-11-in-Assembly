@echo off
REM ===========================================================================
Rem  live-run proof for change 285 (shlwapi!StrCSpnIW).
REM
REM  The shipped export costs 2971 ns over 511 code units, the most expensive of
REM  the remaining StrXxxIW family.
REM
REM  Its relation is change 281's, and that was MEASURED rather than assumed:
REM  probes/relation.c extracted StrCSpnIW's own relation over 786420 pairs using
REM  StrCSpnIW({c},{m}) == 0 as a membership oracle and found zero disagreements,
REM  symmetric, with a multi-member set exactly the union of its members' rows.
REM
REM  The virtual NUL that changes 283 and 284 had to model is UNOBSERVABLE here:
REM  whether the terminator counts as a set member or merely stops the scan, the
REM  answer is the length either way, which is why this change never measures
REM  the string at all.
REM
REM  All the paths are driven: the single unbounded pass for a small set, the
REM  doubling windows for a set spanning several chunks, and the call-free scalar
REM  path with its self, pool and bitmap loops for a set holding an ignorable.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of shlwapi. No system process is touched, nothing on disk is modified, and
REM  the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes\285-strcspniw
set S=%H%..\changes\281-strchriw
cd /d "%H%"
ml64 /nologo /c /Fostrcspniw.obj "%C%\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_strcspniw.c strcspniw.obj "%S%\tables.c" "%S%\foldsets.c" "%S%\foldbig.c" "%S%\foldnul.c" /Fe:live_subst_strcspniw.exe >nul || goto :err
"%H%live_subst_strcspniw.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
