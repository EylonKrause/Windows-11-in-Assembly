@echo off
REM ===========================================================================
Rem  live-run proof for change 280 (ntdll!RtlLargeIntegerToChar).
REM
REM  Every case compares the NTSTATUS AND a hash of the whole 640-byte
REM  destination -- on REFUSING calls too, because probes/contract.c measured
REM  that a refusal leaves the buffer completely untouched.
REM
Rem  five paths are under test: base 10 above 2^32, which peels eight digits at
REM  a time through the one 64-bit reciprocal probes/div64.c proves over the
REM  whole domain; base 10 below 2^32, which never enters that peel; the
REM  single-digit decimal path; bases 2, 8 and 16, which emit several digits per
Rem  store and run to sixty-four characters in binary; and the zero-padded field
REM  WIDTH a negative length asks for -- the path change 100 gets wrong on every
REM  single case, and the only place this change touches an XMM register.
REM
REM  The corpus draws the base from the five legal ones AND the illegal ones, the
REM  value from a distribution covering every one of the sixty-four bit lengths
REM  and the 2^32 and 10^8 seams specifically, and the length from AROUND the
Rem  room rule on both sides of zero. It fails if any path, either sign of
REM  length, or any of the three outcomes comes back thin.
REM
REM  The negative lengths are bounded at 400 into a 640-byte buffer: a field
REM  width is honoured LITERALLY, and probes/contract.c measured that -96 fills
REM  a 96-byte buffer exactly while -97 runs off the end of it.
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
ml64 /nologo /c /Folint2char.obj "%C%\280-rtllargeintegertochar\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_lint2char.c lint2char.obj /Fe:live_subst_lint2char.exe >nul || goto :err
"%H%live_subst_lint2char.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
