@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for changes 209, 210, 211, 225, 227, 229, 240 and 243 -- lstrcpynW + CompareStringOrdinal
REM  + lstrcpynA + lstrlenA + lstrcpyA + lstrcpyW.
REM  A fifth of the corpus is an unterminated source at a PAGE_NOACCESS page:
REM  the export swallows the fault, returns NULL and leaves a partial copy, and
REM  ours must match both the return and exactly how much it copied first.
REM  Sacrificial single-threaded child; no system process is touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Folcpn.obj "%C%\209-lstrcpynw\impl.asm" >nul || goto :err
ml64 /nologo /c /Focso.obj "%C%\210-comparestringordinal\impl.asm" >nul || goto :err
ml64 /nologo /c /Folcpna.obj "%C%\211-lstrcpyna\impl.asm" >nul || goto :err
ml64 /nologo /c /Folena.obj "%C%\225-lstrlena\impl.asm" >nul || goto :err
ml64 /nologo /c /Folcpa.obj "%C%\227-lstrcpya\impl.asm" >nul || goto :err
ml64 /nologo /c /Folcpw2.obj "%C%\229-lstrcpyw\impl.asm" >nul || goto :err
ml64 /nologo /c /Foprfs.obj "%C%\240-pathcchremovefilespec\impl.asm" >nul || goto :err
ml64 /nologo /c /Fopccx.obj "%C%\243-pathcchcanonicalizeex\impl.asm" >nul || goto :err
ml64 /nologo /c /Fopcap.obj "%C%\242-pathcchappendex\impl.asm" >nul || goto :err
REM  209 and 211 both call their exception wrapper seh.c. Compiled in one cl command they
REM  would both land on seh.obj and the second would overwrite the first, so each gets its
REM  own object name.
cl /nologo /O2 /MD /c /Foseh209.obj "%C%\209-lstrcpynw\seh.c" >nul || goto :err
cl /nologo /O2 /MD /c /Foseh211.obj "%C%\211-lstrcpyna\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa /c /Foseh225.obj "%C%\225-lstrlena\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa /c /Foseh227.obj "%C%\227-lstrcpya\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa /c /Foseh229.obj "%C%\229-lstrcpyw\seh.c" >nul || goto :err
cl /nologo /O2 /MD /EHa live_subst_kernelbase.c "%C%\210-comparestringordinal\wrapper.c" "%C%\210-comparestringordinal\upcase.c" seh209.obj seh211.obj seh225.obj seh227.obj seh229.obj lcpn.obj cso.obj lcpna.obj lena.obj lcpa.obj lcpw2.obj prfs.obj pccx.obj pcap.obj /Fe:live_subst_kernelbase.exe >nul || goto :err
"%H%live_subst_kernelbase.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
