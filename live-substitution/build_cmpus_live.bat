@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 263 (ntdll!RtlCompareUnicodeStrings).
REM
REM  The EXACT LONG is compared, not its sign: this export returns the
REM  DIFFERENCE of the two characters, so an implementation returning -1/0/1
REM  would satisfy every caller that writes "< 0" and every check that only
REM  looked at the sign.
REM
REM  The corpus is built to produce all three answers -- equal, decided by a
REM  character, decided by the LENGTHS -- and to put a character at or above
REM  0x80 in one case in four, because our case-insensitive path folds ASCII
REM  in-vector and goes through the upcase table otherwise. A pure-ASCII corpus
REM  would leave the table path untested while looking thorough.
REM
REM  Change 210's upcase table is linked in: it is built at run time from
REM  RtlUpcaseUnicodeChar, so there is one place where this project decides
REM  what the fold is.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of ntdll. No system process is touched, nothing on disk is modified,
REM  and the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Focmpus.obj "%C%\263-rtlcompareunicodestrings\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_cmpus.c "%C%\210-comparestringordinal\upcase.c" cmpus.obj /Fe:live_subst_cmpus.exe >nul || goto :err
"%H%live_subst_cmpus.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
