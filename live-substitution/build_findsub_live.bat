@echo off
REM ===========================================================================
Rem  live-run proof for change 252 (ntdll!RtlFindUnicodeSubstring).
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of ntdll. No system process is touched, nothing on disk is modified,
REM  and the prologue is restored and verified byte-for-byte.
REM
REM  casemate.c is linked because the insensitive path compares against the
REM  CASE-PARTNER table it derives from change 210's OS-built upcase table. It
REM  must be INITIALISED too, and the harness does that first and says so: an
REM  unbuilt table here fails silently, not loudly.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fofsub.obj "%C%\252-rtlfindunicodesubstring\impl.asm"          >nul || goto :err
cl /nologo /O2 /c /Fofsub_cm.obj "%C%\252-rtlfindunicodesubstring\casemate.c"   >nul || goto :err
cl /nologo /O2 /c /Fofsub_up.obj "%C%\210-comparestringordinal\upcase.c"        >nul || goto :err
cl /nologo /O2 live_subst_findsub.c fsub.obj fsub_cm.obj fsub_up.obj /Fe:live_subst_findsub.exe >nul || goto :err
"%H%live_subst_findsub.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
