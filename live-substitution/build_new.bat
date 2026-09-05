@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fo sr.obj   "%C%\070-strrev\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fo wr.obj   "%C%\071-wcsrev\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fo ul.obj   "%C%\072-ultow\impl.asm"     >nul || goto :err
ml64 /nologo /c /Fo u64.obj  "%C%\073-ui64tow\impl.asm"   >nul || goto :err
ml64 /nologo /c /Fo itw.obj  "%C%\074-itow\impl.asm"      >nul || goto :err
ml64 /nologo /c /Fo i64.obj  "%C%\075-i64tow\impl.asm"    >nul || goto :err
cl /nologo /O2 /c "%C%\070-strrev\reference.c"  /Foref_sr.obj  >nul || goto :err
cl /nologo /O2 /c "%C%\071-wcsrev\reference.c"  /Foref_wr.obj  >nul || goto :err
cl /nologo /O2 /c "%C%\072-ultow\reference.c"   /Foref_ul.obj  >nul || goto :err
cl /nologo /O2 /c "%C%\073-ui64tow\reference.c" /Foref_u64.obj >nul || goto :err
cl /nologo /O2 /c "%C%\074-itow\reference.c"    /Foref_it.obj  >nul || goto :err
cl /nologo /O2 /c "%C%\075-i64tow\reference.c"  /Foref_i64.obj >nul || goto :err
cl /nologo /O2 /c "%C%\072-ultow\dec2.c"        /Fodec2n.obj   >nul || goto :err
cl /nologo /O2 /c live_subst_new.c /Folsn.obj >nul || goto :err
link /nologo /OUT:live_subst_new.exe lsn.obj sr.obj wr.obj ul.obj u64.obj itw.obj i64.obj ref_sr.obj ref_wr.obj ref_ul.obj ref_u64.obj ref_it.obj ref_i64.obj dec2n.obj >nul || goto :err
"%H%live_subst_new.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
