@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 267 (ntdll!RtlCrc32).
REM
REM  A checksum is the easiest thing in this project to get subtly wrong and the
REM  hardest to notice: one wrong constant gives a perfectly plausible 32-bit
REM  number at a perfectly plausible speed. Every case compares the exact value,
REM  and the corpus is built around the two block boundaries the implementation
REM  has -- 192 and 3072 bytes -- rather than around round numbers.
REM
REM  Most cases use a NON-ZERO initial CRC, because it enters only the FIRST of
REM  the three parallel chains: an implementation that seeded the wrong chain
REM  would pass every case that started from zero.
REM
REM  shifttab.c is linked in: it builds the two "advance past N zero bytes"
REM  tables from the polynomial at run time and checks them against their own
REM  definition before anything else runs.
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
ml64 /nologo /c /Focrc32.obj "%C%\267-rtlcrc32\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_crc32.c "%C%\267-rtlcrc32\shifttab.c" crc32.obj /Fe:live_subst_crc32.exe >nul || goto :err
"%H%live_subst_crc32.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
