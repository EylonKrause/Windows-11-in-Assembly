@echo off
REM ===========================================================================
Rem  live-run proof for change 278 (ntdll!RtlIntegerToUnicodeString).
REM
REM  Every case compares the NTSTATUS, Length, MaximumLength AND a hash of the
REM  whole destination -- on REFUSING calls too, because probes/contract.c
REM  measured that a refusal leaves the UNICODE_STRING completely untouched, with
REM  Length keeping whatever the caller had in it. An implementation that zeroed
REM  Length on the way out would pass any check that only read the status.
REM
Rem  two converters are under test: base 10 is length-first and two digits at a
REM  time, and bases 2, 8 and 16 share a shift-and-mask loop. The corpus draws the
REM  base from the five legal ones AND the illegal ones, the value from the
REM  digit-count boundaries as often as from anywhere, and MaximumLength from
REM  AROUND the room rule -- which is Length+2 here and Length+1 in the routine
REM  change 067 owns, one byte apart in the same DLL. The harness FAILS if either
REM  converter, or any of the three outcomes, comes back thin.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of ntdll. No system process is touched, nothing on disk is modified, and the
REM  prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Foint2ustr.obj "%C%\278-rtlintegertounicodestring\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_int2ustr.c int2ustr.obj /Fe:live_subst_int2ustr.exe >nul || goto :err
"%H%live_subst_int2ustr.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
