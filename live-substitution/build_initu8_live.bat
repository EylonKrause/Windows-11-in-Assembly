@echo off
REM ===========================================================================
Rem  live-run proof for change 264 (ntdll!RtlInitUTF8String).
REM
REM  The implementation is change 095's, reached through a linker ALIAS:
REM  probes/equiv.c proved RtlInitUTF8String identical to RtlInitString over
REM  125883 cases, every ordered byte pair included. This run is what turns that
REM  into a claim about THIS export -- the patch goes on RtlInitUTF8String at
REM  its own address, and every answer is compared against what the shipped code
REM  at that address produced before the patch existed.
REM
REM  All THREE fields are compared against a struct poisoned with 0xCD, and the
REM  corpus reaches both the 0xFFFF clamp and the NULL source, which are the two
REM  cases an implementation is most likely to get partly right.
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
ml64 /nologo /c /Foinitu8.obj "%C%\264-rtlinitutf8string\impl.asm" >nul || goto :err
ml64 /nologo /c /Foinitu8dep.obj "%C%\095-rtlinitstring\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_initu8.c initu8.obj initu8dep.obj /Fe:live_subst_initu8.exe >nul || goto :err
"%H%live_subst_initu8.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
