@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 271 (advapi32!ConvertSidToStringSidA).
REM
REM  The same five comparisons as the wide form's harness -- the BOOL,
REM  GetLastError, what happened to the output pointer, LocalSize and LocalFlags,
REM  and a hash of every byte of the block -- with the block one byte per
REM  character rather than two.
REM
REM  THE NARROWING IS WHAT IS ON TRIAL HERE. VPACKUSWB saturates, so a character
REM  at or above 0x100 would come back as 0xFF rather than as itself, and only a
REM  byte comparison shows that. probes/contract.c established that a SID string
REM  is ASCII over every shape of SID and under four thread locales; this gate
REM  asks the same question with the bytes compared against the live export.
REM
REM  Three changes are linked and none is copied: change 067's formatter, change
REM  270's alloc.c, and 271's own pack.
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
ml64 /nologo /c /Fosid2stra.obj "%C%\271-convertsidtostringsida\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosid2strafmt.obj "%C%\067-rtlconvertsidtounicodestring\impl.asm" >nul || goto :err
cl /nologo /O2 /EHa /c /Fosid2straprb.obj "%C%\067-rtlconvertsidtounicodestring\probe.c" >nul || goto :err
cl /nologo /O2 /c /Fosid2straalc.obj "%C%\270-convertsidtostringsid\alloc.c" >nul || goto :err
cl /nologo /O2 live_subst_sid2stra.c sid2stra.obj sid2strafmt.obj sid2straprb.obj sid2straalc.obj advapi32.lib /Fe:live_subst_sid2stra.exe >nul || goto :err
"%H%live_subst_sid2stra.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
