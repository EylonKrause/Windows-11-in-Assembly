@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 279 (ntdll!RtlIntegerToChar).
REM
REM  Every case compares the NTSTATUS AND a hash of the whole 512-byte
REM  destination -- on REFUSING calls too, because probes/contract.c measured
REM  that a refusal leaves the buffer completely untouched. An implementation
REM  that wrote a terminator before discovering it had no room would pass any
REM  check that only read the status.
REM
REM  THREE WRITE PATHS ARE UNDER TEST: base 10 is length-first and two digits at
REM  a time; bases 2, 8 and 16 emit several digits per store; and a NEGATIVE length
REM  is a ZERO-PADDED FIELD WIDTH, a fill loop that no positive length reaches
REM  and the only place this change touches an XMM register. The corpus draws the
REM  base from the five legal ones AND the illegal ones, the value from the
REM  digit-count boundaries as often as from anywhere, and the length from AROUND
REM  the room rule on BOTH sides of zero. It FAILS if any converter, either sign
REM  of length, the wide fill, or any of the three outcomes comes back thin.
REM
REM  The negative lengths are bounded at 300 into a 512-byte buffer: a field
REM  width is honoured LITERALLY, and probes/negative.c measured that a width
REM  longer than the buffer faults.
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
ml64 /nologo /c /Foint2char.obj "%C%\279-rtlintegertochar\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_int2char.c int2char.obj /Fe:live_subst_int2char.exe >nul || goto :err
"%H%live_subst_int2char.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
