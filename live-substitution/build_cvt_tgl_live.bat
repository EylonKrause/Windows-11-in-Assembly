@echo off
REM ===========================================================================
Rem  live substitution, tgl variant of 290.
REM
REM  Identical to build_cvt_live.bat in every respect except ONE line: change
REM  290 is assembled from impl_tgl.asm instead of impl.asm, so this hot-patches
REM  kernelbase!MultiByteToWideChar with the AVX512VBMI2 variant and proves IT
REM  runs in the live process, rather than the AVX2 implementation of record.
REM  289 and 291 are the unchanged parents, so anything this reports about them
REM  is the same result build_cvt_live.bat already gives.
REM
REM  Bench #3 only: impl_tgl.asm needs AVX-512, which is why it is a variant.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fo cvt_wc2mb.obj  "%C%\289-widechartomultibyte\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fo cvt_mbtwc.obj  "%C%\290-multibytetowidechar\impl_tgl.asm"        >nul || goto :err
ml64 /nologo /c /Fo cvt_expand.obj "%C%\291-expandenvironmentstringsw\impl.asm"  >nul || goto :err

cl /nologo /O2 /MD /c live_subst_cvt.c /Folsc_tgl.obj >nul || goto :err
link /nologo /OUT:live_subst_cvt_tgl.exe lsc_tgl.obj cvt_wc2mb.obj cvt_mbtwc.obj cvt_expand.obj ^
     kernel32.lib >nul || goto :err

"%H%live_subst_cvt_tgl.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
