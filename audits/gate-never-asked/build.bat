@echo off
REM ===========================================================================
REM  audits/gate-never-asked -- does the itoa/ultoa family agree with ucrtbase
REM  on a radix outside 2..36?
REM
REM  Eight landed changes reimplement this family and every one of their corpora
REM  swept exactly `for (int radix = 2; radix <= 36; ++radix)`. The radix is a
REM  SIGNED int in all eight signatures. That is the same shape as the defect
REM  found in changes 097 and 100: a parameter class the gate never asked about.
REM
REM  ONE CALL PER PROCESS. The all-in-one first draft died at exit code 148
REM  having printed nothing, which is the signature of a fail-fast -- an
REM  uncatchable termination that __except cannot see. The only way to attribute
REM  one to a side is to give each call its own process and read the exit code.
REM  Changes 274 and 276 hit the same wall on SysFreeString.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\..\changes
cd /d "%H%"
ml64 /nologo /c /Fort_ultoa.obj   "%C%\054-ultoa\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fort_itoa.obj    "%C%\056-itoa\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fort_ui64toa.obj "%C%\055-ui64toa\impl.asm" >nul || goto :err
ml64 /nologo /c /Fort_i64toa.obj  "%C%\057-i64toa\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fort_ultow.obj   "%C%\072-ultow\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fort_itow.obj    "%C%\074-itow\impl.asm"    >nul || goto :err
cl /nologo /O2 /c /Fort_d2b.obj "%C%\054-ultoa\dec2b.c" >nul || goto :err
cl /nologo /O2 /c /Fort_d2w.obj "%C%\072-ultow\dec2.c"  >nul || goto :err
cl /nologo /O2 radix_one.c rt_ultoa.obj rt_itoa.obj rt_ui64toa.obj rt_i64toa.obj ^
   rt_ultow.obj rt_itow.obj rt_d2b.obj rt_d2w.obj /Fe:radix_one.exe >nul || goto :err
py "%H%radix_driver.py"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
