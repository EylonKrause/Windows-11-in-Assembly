@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 254 (kernelbase!FindStringOrdinal).
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of kernelbase. No system process is touched, nothing on disk is
REM  modified, and the prologue is restored and verified byte-for-byte.
REM
REM  BOTH observables are compared -- the returned index AND GetLastError() --
REM  because our implementation writes the last error straight to the TEB at
REM  gs:[0x68] rather than calling SetLastError, which is what the shipped code
REM  does too. Writing a TEB field by hand is exactly the kind of thing that
REM  works in a unit test and fails in a real process, so it is proved here.
REM
REM  Links change 001 (wcslen, for a cch of -1) and change 252's case-partner
REM  table, which casemate.c derives from change 210's OS-built upcase table --
REM  and INITIALISES it, because an unbuilt table here fails silently.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fofso.obj      "%C%\254-findstringordinal\impl.asm"          >nul || goto :err
ml64 /nologo /c /Fofso_len.obj  "%C%\001-wcslen\impl.asm"                     >nul || goto :err
cl /nologo /O2 /c /Fofso_cm.obj "%C%\252-rtlfindunicodesubstring\casemate.c"  >nul || goto :err
cl /nologo /O2 /c /Fofso_up.obj "%C%\210-comparestringordinal\upcase.c"       >nul || goto :err
cl /nologo /O2 live_subst_fso.c fso.obj fso_len.obj fso_cm.obj fso_up.obj /Fe:live_subst_fso.exe >nul || goto :err
"%H%live_subst_fso.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
