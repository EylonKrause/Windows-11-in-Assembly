@echo off
REM ===========================================================================
Rem  live-run proof for change 273 (ws2_32!inet_addr).
REM
REM  This patches ws2_32!inet_addr ITSELF, which build_ws2_live.bat does not: that
REM  one patches ntdll!RtlIpv4StringToAddressA (change 114) and shows the counter
REM  moving through ws2_32, which proves inet_addr DELEGATES to it, and it does.
REM  What it cannot prove is the stronger claim above it, that inet_addr "does not
REM  parse an address at all": changes/273-inet-addr/probes/grammar.c asks both the
REM  same ten questions and they disagree on six, because inet_addr falls back to
REM  its own far more permissive grammar whenever the ntdll call refuses.
REM
REM  The corpus is built from what the probes found, the wrapping accumulator,
REM  the whitespace terminator, every field boundary in all three bases, and not
REM  from plausible dotted quads, which exercise none of it.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of ws2_32. No system process is touched, nothing on disk is modified, and the
REM  prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Foinetaddr.obj "%C%\273-inet-addr\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_inetaddr.c inetaddr.obj ws2_32.lib /Fe:live_subst_inetaddr.exe >nul || goto :err
"%H%live_subst_inetaddr.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
