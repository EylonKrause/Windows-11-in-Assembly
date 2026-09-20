@echo off
REM ===========================================================================
Rem  live-run proof for change 275 (ws2_32!inet_ntoa).
REM
REM  The answer lives in a PER-THREAD buffer that the next call overwrites, so
REM  every answer is copied the instant it comes back. A harness that kept the
REM  pointer and compared it later would be comparing a string against whatever
REM  the most recent call left there, and would pass whatever either
REM  implementation did.
REM
Rem  it runs on four threads. a single-threaded harness cannot tell a per-thread
REM  buffer from a per-process one, and "per-thread" is the only part of this
REM  contract a wrong implementation could satisfy on one thread and break on two.
Rem  The workers are created after the patch is in place and joined before it is
REM  removed, so no thread is ever inside the sixteen bytes being written.
REM
REM  tls.c is linked in: it owns our thread-local buffer, and the compiler emits
REM  the gs:[0x58] walk with the right relocations.
REM
REM  Sacrificial child; it patches only its own copy-on-write copy of ws2_32. No
REM  system process is touched, nothing on disk is modified, and the prologue is
REM  restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Foinetntoa.obj "%C%\275-inet-ntoa\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Foinetntoatls.obj "%C%\275-inet-ntoa\tls.c" >nul || goto :err
cl /nologo /O2 live_subst_inetntoa.c inetntoa.obj inetntoatls.obj ws2_32.lib /Fe:live_subst_inetntoa.exe >nul || goto :err
"%H%live_subst_inetntoa.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
