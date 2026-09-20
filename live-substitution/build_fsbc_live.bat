@echo off
REM ===========================================================================
Rem  live-run proof for change 262 (ntdll!RtlFindSetBitsAndClear,
REM  ntdll!RtlFindClearBitsAndSet).
REM
Rem  Both exports are patched one at a time, each with its own counter, and what
Rem  is compared is the answer *and* the bitmap the call left behind -- the
REM  mutation is the half this change adds, so a run that only checked return
REM  values would be testing change 256 and calling it 262.
REM
REM  Change 256's assembly is linked in: probes/equiv.c measured 262's search to
REM  be exactly RtlFindSetBits over 320000 calls, so the search is not
REM  reimplemented here and the object it lives in has to be present.
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
ml64 /nologo /c /Fofsbc.obj "%C%\262-rtlfindsetbitsandclear\impl.asm" >nul || goto :err
ml64 /nologo /c /Fofsbc256.obj "%C%\256-rtlfindsetbits\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_fsbc.c fsbc.obj fsbc256.obj /Fe:live_subst_fsbc.exe >nul || goto :err
"%H%live_subst_fsbc.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
