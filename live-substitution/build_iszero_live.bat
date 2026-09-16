@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 266 (ntdll!RtlIsZeroMemory).
REM
REM  A predicate has only two answers, which makes a careless corpus very easy
REM  to pass: an implementation that always said "not zero" would agree with the
REM  shipped export on nearly every random buffer. So the corpus produces BOTH
REM  answers in quantity, puts the first non-zero byte at EVERY position
REM  including the very last, and keeps a fifth of its cases below 32 bytes,
REM  where the overlapping ladder runs instead of the vector loops. The run
REM  reports each of those counts and fails if any is thin.
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
ml64 /nologo /c /Foiszero.obj "%C%\266-rtliszeromemory\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_iszero.c iszero.obj /Fe:live_subst_iszero.exe >nul || goto :err
"%H%live_subst_iszero.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
