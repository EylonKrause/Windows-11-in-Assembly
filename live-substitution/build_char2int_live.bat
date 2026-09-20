@echo off
REM ===========================================================================
Rem  live-run proof for change 129 (ntdll!RtlCharToInteger).
REM
REM  This change was PARKED before gate 4 existed, so this is the first time the
REM  shipped export is actually replaced by it.
REM
Rem  Every case compares the NTSTATUS and the caller's ulong, with that ulong
REM  pre-poisoned to a sentinel, because an invalid base must leave it
REM  COMPLETELY untouched, and zeroing the output first is the natural way to
REM  write the code and would be wrong. A harness reading only the status would
REM  pass that.
REM
Rem  both invalid-base mechanisms are driven. The landing edit validates a
REM  caller-supplied base with a range test plus a bitmask over bits 2, 8 and 16
REM  instead of a four-compare ladder, which splits the illegal bases into two
REM  populations, above 16 (the range test) and below 16 but not in the set
REM  (the mask). A corpus using only base 36 would exercise one of them.
REM
REM  It also plants what the contract rules need: the three LOWERCASE-only
REM  prefixes and their uppercase twins, which are NOT prefixes; a leading '0'
REM  followed by a byte outside the b..x window, which means DECIMAL; high bytes
REM  0x80-0xFF for the SIGNED-char whitespace skip to eat; and 15-to-20 digit
REM  decimals for the silent mod-2^32 wrap.
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
ml64 /nologo /c /Fochar2int.obj "%C%\129-rtlchartointeger\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_char2int.c char2int.obj /Fe:live_subst_char2int.exe >nul || goto :err
"%H%live_subst_char2int.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
