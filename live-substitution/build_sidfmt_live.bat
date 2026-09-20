@echo off
REM ===========================================================================
Rem  live-run proof for change 067 (ntdll!RtlConvertSidToUnicodeString).
REM
REM  Every case compares the NTSTATUS, Length, MaximumLength AND a hash of all
REM  1024 destination bytes, because this export's interesting behaviour is at
REM  the EDGE of the caller's buffer: at MaximumLength == Length+1 it succeeds
REM  and writes NO terminator, and below that it refuses and leaves Out entirely
REM  untouched. MaximumLength is therefore drawn from AROUND the boundary rather
REM  than from "something ample", and the sub-authority count is drawn over its
REM  whole byte range rather than 0..15 -- the two things the gate this replaces
REM  could not express.
REM
REM  probe.c is linked in: the revision, the count and the last sub-authority are
REM  read under an exception handler and the identifier authority is not, which
REM  is what the live export does.
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
ml64 /nologo /c /Fosidfmt.obj "%C%\067-rtlconvertsidtounicodestring\impl.asm" >nul || goto :err
cl /nologo /O2 /EHa live_subst_sidfmt.c "%C%\067-rtlconvertsidtounicodestring\probe.c" sidfmt.obj /Fe:live_subst_sidfmt.exe >nul || goto :err
"%H%live_subst_sidfmt.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
