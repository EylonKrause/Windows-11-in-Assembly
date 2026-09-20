@echo off
REM ===========================================================================
Rem  live-run proof for change 269 (advapi32!ConvertStringSidToSidW).
REM
REM  This export ALLOCATES. Every success hands the caller a LocalAlloc block
REM  that the caller frees through the ordinary, UNPATCHED LocalFree, so the
REM  harness frees every one of them -- an implementation that returned a static
REM  buffer or a HeapAlloc block would pass correctness.c and corrupt the heap
REM  here.
REM
Rem  Four things are compared per case: the BOOL, GetLastError, what happened to
Rem  the output pointer (a poison value distinguishes "left alone" from "cleared
REM  to NULL" from "written"), and every byte of the SID. The third of those is
REM  not decoration: `)`, `,` and `;` after a complete SID are the only
REM  characters in the 16-bit space that make a FAILING call write the pointer.
REM
REM  aliases.c and classify.c are linked in and initialised BEFORE the patch
REM  exists -- both build themselves by asking this very export several thousand
REM  questions, and under the patch they would be asking our code what our code
REM  should say.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of the module. advapi32 FORWARDS this export, so the bytes patched are
REM  sechost's -- the harness prints which module it landed in. No system process
REM  is touched, nothing on disk is modified, and the prologue is restored and
REM  verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fosid269.obj "%C%\269-convertstringsidtosid\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_sid.c "%C%\269-convertstringsidtosid\aliases.c" "%C%\269-convertstringsidtosid\classify.c" sid269.obj advapi32.lib user32.lib /Fe:live_subst_sid.exe >nul || goto :err
"%H%live_subst_sid.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
