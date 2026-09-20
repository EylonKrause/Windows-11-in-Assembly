@echo off
REM ===========================================================================
Rem  live-run proof for change 268 (ntdll!RtlUnicodeStringToUTF8String and
REM  ntdll!RtlUTF8StringToUnicodeString).
REM
REM  both exports are patched at once. The whole point of 268 is that the two
REM  directions do four things differently -- what a failing call leaves in the
REM  buffer, whether STATUS_SOME_NOT_MAPPED survives, which of two failure codes
REM  a shortfall gets, and how much room the terminator needs -- so a proof that
REM  patched only one of them would be a proof about half a change.
REM
REM  Every case compares the NTSTATUS, Length, MaximumLength AND a hash of the
REM  whole destination buffer, because these functions leave a destination
REM  partially written on a failing call in one direction and untouched in the
REM  other. The allocating path is in the corpus and every block it returns is
REM  freed through the UNPATCHED RtlFreeUTF8String / RtlFreeUnicodeString.
REM
REM  016 and 034 are linked in: this change is a wrapper over the N-forms this
REM  project already converted, not a second copy of them.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of ntdll. No system process is touched, nothing on disk is modified,
REM  and both prologues are restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fou8str.obj "%C%\268-rtlunicodestringtoutf8string\impl.asm" >nul || goto :err
ml64 /nologo /c /Fou8str016.obj "%C%\016-rtlunicodetoutf8n\impl.asm" >nul || goto :err
ml64 /nologo /c /Fou8str034.obj "%C%\034-rtlutf8tounicoden\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_u8str.c "%C%\268-rtlunicodestringtoutf8string\heapalloc.c" u8str.obj u8str016.obj u8str034.obj /Fe:live_subst_u8str.exe >nul || goto :err
"%H%live_subst_u8str.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
