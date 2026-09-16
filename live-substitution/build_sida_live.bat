@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 272 (advapi32!ConvertStringSidToSidA).
REM
REM  Four things per case: the BOOL, GetLastError, what happened to the output
REM  pointer, and every byte of the SID. Every allocated SID is freed through the
REM  process's UNPATCHED LocalFree.
REM
REM  THE LAST ERROR IS COMPARED ON EVERY CALL, FROM A NON-ZERO SENTINEL. Change
REM  269's first gate did neither, and the two omissions together hid a real
REM  defect for a whole change: all four exports of this family ZERO the last
REM  error on success and 269's implementation did not.
REM
REM  Both widening paths are driven -- the VPMOVZXBW zero extension for pure
REM  ASCII and MultiByteToWideChar for anything with a high byte -- and the
REM  harness FAILS if the high-byte class comes back empty, because a corpus of
REM  plausible SID strings never reaches it. Alignment is swept 0..63, since the
REM  scan loads its first block aligned down.
REM
REM  Change 269's assembly and both of its OS-derived tables are linked in; the
REM  tables are built BEFORE the patch exists, because they build themselves by
REM  asking the wide export several thousand questions.
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
ml64 /nologo /c /Fosida.obj "%C%\272-convertstringsidtosida\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosidaparse.obj "%C%\269-convertstringsidtosid\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Fosidaal.obj "%C%\269-convertstringsidtosid\aliases.c" >nul || goto :err
cl /nologo /O2 /c /Fosidacl.obj "%C%\269-convertstringsidtosid\classify.c" >nul || goto :err
cl /nologo /O2 /c /Fosidawd.obj "%C%\272-convertstringsidtosida\widen.c" >nul || goto :err
cl /nologo /O2 live_subst_sida.c sida.obj sidaparse.obj sidaal.obj sidacl.obj sidawd.obj advapi32.lib user32.lib /Fe:live_subst_sida.exe >nul || goto :err
"%H%live_subst_sida.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
