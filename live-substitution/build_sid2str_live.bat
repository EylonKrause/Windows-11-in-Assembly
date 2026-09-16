@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 270 (advapi32!ConvertSidToStringSidW).
REM
REM  This export ALLOCATES. Every success hands the caller a LocalAlloc block
REM  that the caller frees through the ordinary, UNPATCHED LocalFree, so the
REM  harness frees every one of them -- and compares LocalSize and LocalFlags as
REM  well as the bytes, because a block that is right but too big is still wrong.
REM
REM  Five things per case: the BOOL, GetLastError (which on success becomes ZERO
REM  whatever it was before), what happened to the output pointer (a poison value
REM  distinguishes "left alone" from "cleared" from "written"), LocalSize and
REM  LocalFlags, and a hash of every byte of the block.
REM
REM  CHANGE 067 IS LINKED IN, NOT COPIED: this change is an envelope over it, and
REM  probes/contract.c established that advapi32's formatter and ntdll's produce
REM  the same text for every shape of SID and refuse the same ones. probe.c comes
REM  with it, because 067 reads the revision, the count and the last
REM  sub-authority under an exception handler and the identifier authority
REM  without one.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of the module. No system process is touched, nothing on disk is
REM  modified, and the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fosid2str.obj "%C%\270-convertsidtostringsid\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosid2strfmt.obj "%C%\067-rtlconvertsidtounicodestring\impl.asm" >nul || goto :err
cl /nologo /O2 /EHa /c /Fosid2strprb.obj "%C%\067-rtlconvertsidtounicodestring\probe.c" >nul || goto :err
cl /nologo /O2 /c /Fosid2stralc.obj "%C%\270-convertsidtostringsid\alloc.c" >nul || goto :err
cl /nologo /O2 live_subst_sid2str.c sid2str.obj sid2strfmt.obj sid2strprb.obj sid2stralc.obj advapi32.lib /Fe:live_subst_sid2str.exe >nul || goto :err
"%H%live_subst_sid2str.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
