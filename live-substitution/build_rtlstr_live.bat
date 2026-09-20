@echo off
REM ===========================================================================
REM  Live substitution for the six ntdll counted-string comparison exports:
REM    009 RtlHashUnicodeString   010 RtlEqualUnicodeString
REM    011 RtlPrefixUnicodeString 012 RtlCompareString
REM    013 RtlEqualString         014 RtlPrefixString
REM
Rem  one upcase.c and one upcase_ansi.c are linked for all six. Changes 010 and
REM  011 ship byte-identical upcase.c, 012/013/014 ship byte-identical
REM  upcase_ansi.c, and 009's upcase.c differs from 010's only in whitespace and
REM  a comment, they all define wia_upcase / wia_upcase_init (and the ANSI
REM  pair), so linking more than one is a duplicate-symbol error rather than a
REM  choice. Sharing them is also what makes a case-fold bug show up in five
REM  places at once instead of one.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Forls009.obj "%C%\009-rtlhashunicodestring\impl.asm"   >nul || goto :err
ml64 /nologo /c /Forls010.obj "%C%\010-rtlequalunicodestring\impl.asm"  >nul || goto :err
ml64 /nologo /c /Forls011.obj "%C%\011-rtlprefixunicodestring\impl.asm" >nul || goto :err
ml64 /nologo /c /Forls012.obj "%C%\012-rtlcomparestring\impl.asm"       >nul || goto :err
ml64 /nologo /c /Forls013.obj "%C%\013-rtlequalstring\impl.asm"         >nul || goto :err
ml64 /nologo /c /Forls014.obj "%C%\014-rtlprefixstring\impl.asm"        >nul || goto :err

cl /nologo /O2 /c /Forlsupw.obj "%C%\010-rtlequalunicodestring\upcase.c"      >nul || goto :err
cl /nologo /O2 /c /Forlsupa.obj "%C%\012-rtlcomparestring\upcase_ansi.c"      >nul || goto :err

cl /nologo /O2 live_subst_rtlstr.c rls009.obj rls010.obj rls011.obj rls012.obj rls013.obj rls014.obj ^
   rlsupw.obj rlsupa.obj /Fe:live_subst_rtlstr.exe >nul || goto :err

"%H%live_subst_rtlstr.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
