@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 258 (ntdll!RtlFindClearRuns).
REM
REM  ONE export, but its two forms are patched and driven SEPARATELY, each with
REM  its own counter: SortByLength selects between a 64-bit word scan and a
REM  byte scan whose emission order is the thing being matched, so a harness
REM  that mixed them could pass while one of them was wrong.
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
ml64 /nologo /c /Fofcr.obj "%C%\258-rtlfindclearruns\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_fcr.c fcr.obj /Fe:live_subst_fcr.exe >nul || goto :err
"%H%live_subst_fcr.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
