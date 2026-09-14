@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for changes 192 (RtlAreBitsClear) and 193 (RtlIsTextUnicode).
REM  Sacrificial single-threaded child; no system process is touched, nothing on
REM  disk is modified. 193 compares the BOOL and the rewritten *lpi on every case.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fon2_abc.obj "%C%\192-rtlarebitsclear\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fon2_itu.obj "%C%\193-rtlistextunicode\impl.asm"  >nul || goto :err
cl /nologo /O2 live_subst_ntdll2.c n2_abc.obj n2_itu.obj /Fe:live_subst_ntdll2.exe >nul || goto :err
"%H%live_subst_ntdll2.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
