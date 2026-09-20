@echo off
REM ===========================================================================
REM  Live substitution for the four ntdll bitmap routines:
REM    023 RtlNumberOfSetBits       030 RtlAreBitsSet
REM    123 RtlFindLongestRunClear   124 RtlNumberOfClearBits
REM  All four are self-contained impl.asm -- no helper .c, no init call.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fobm023.obj "%C%\023-rtlnumberofsetbits\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fobm030.obj "%C%\030-rtlarebitsset\impl.asm"            >nul || goto :err
ml64 /nologo /c /Fobm123.obj "%C%\123-rtlfindlongestrunclear\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fobm124.obj "%C%\124-rtlnumberofclearbits\impl.asm"     >nul || goto :err

cl /nologo /O2 live_subst_bitmap.c bm023.obj bm030.obj bm123.obj bm124.obj ^
   /Fe:live_subst_bitmap.exe >nul || goto :err

"%H%live_subst_bitmap.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
