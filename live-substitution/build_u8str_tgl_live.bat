@echo off
REM ===========================================================================
Rem  live substitution, tgl variant of 034.
REM
REM  build_u8str_live.bat with exactly one line changed: change 034 is assembled
REM  from impl_tgl.asm instead of impl.asm.  This harness patches change 268's
REM  wrapper over the live ntdll exports, and that wrapper CALLS 034's decoder --
REM  so what actually converts the bytes inside the patched export here is the
REM  AVX512VBMI2 variant, not the AVX2 implementation of record.
REM
REM  It is the gate that matters most for this particular variant: 034's TGL
REM  decoder passed 327758 correctness cases while silently skipping most of its
REM  output on malformed input.  See changes/034-.../RESULTS-tgl.md.
REM
REM  Bench #3 only: impl_tgl.asm needs AVX-512, which is why it is a variant.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fou8str.obj "%C%\268-rtlunicodestringtoutf8string\impl.asm" >nul || goto :err
ml64 /nologo /c /Fou8str016.obj "%C%\016-rtlunicodetoutf8n\impl.asm" >nul || goto :err
ml64 /nologo /c /Fou8str034tgl.obj "%C%\034-rtlutf8tounicoden\impl_tgl.asm" >nul || goto :err
cl /nologo /O2 live_subst_u8str.c "%C%\268-rtlunicodestringtoutf8string\heapalloc.c" u8str.obj u8str016.obj u8str034tgl.obj /Fe:live_subst_u8str_tgl.exe >nul || goto :err
"%H%live_subst_u8str_tgl.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
