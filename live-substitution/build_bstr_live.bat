@echo off
REM ===========================================================================
REM  Live substitution for change 299, oleaut32!SysAllocString.
REM  299 is PARKED on speed (its bench cannot resolve an allocator-dominated
REM  subject) but it is the only change that TAIL-JUMPS into an import, and that
REM  structure is only really exercised when the call arrives through a patched
REM  export rather than from a direct call site.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fobs299.obj "%C%\299-sysallocstring\impl.asm" >nul || goto :err

cl /nologo /O2 live_subst_bstr.c bs299.obj oleaut32.lib /Fe:live_subst_bstr.exe >nul || goto :err

"%H%live_subst_bstr.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
