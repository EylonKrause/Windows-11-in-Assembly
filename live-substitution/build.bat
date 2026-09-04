@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fowcslen.obj "%C%\001-wcslen\impl.asm" >nul || goto :err
ml64 /nologo /c /Fomemchr.obj "%C%\002-memchr\impl.asm" >nul || goto :err
ml64 /nologo /c /Fowcschr.obj "%C%\003-wcschr\impl.asm" >nul || goto :err
ml64 /nologo /c /Fowcscmp.obj "%C%\004-wcscmp\impl.asm" >nul || goto :err
ml64 /nologo /c /Forcm.obj "%C%\007-rtlcomparememory\impl.asm" >nul || goto :err
ml64 /nologo /c /Forcu.obj "%C%\008-rtlcompareunicodestring\impl.asm" >nul || goto :err
ml64 /nologo /c /Foupcasestr.obj "%C%\015-rtlupcaseunicodestring\impl.asm" >nul || goto :err
ml64 /nologo /c /Fowi.obj "%C%\042-wcsicmp\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosi.obj "%C%\043-stricmp\impl.asm" >nul || goto :err
ml64 /nologo /c /Fomi.obj "%C%\046-memicmp\impl.asm" >nul || goto :err
ml64 /nologo /c /Fowp.obj "%C%\035-wcspbrk\impl.asm" >nul || goto :err
ml64 /nologo /c /Fosp.obj "%C%\038-strpbrk\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Foref_wcslen.obj "%C%\001-wcslen\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_memchr.obj "%C%\002-memchr\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_wcschr.obj "%C%\003-wcschr\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_wcscmp.obj "%C%\004-wcscmp\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_rcm.obj "%C%\007-rtlcomparememory\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_rcu.obj "%C%\008-rtlcompareunicodestring\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_upcasestr.obj "%C%\015-rtlupcaseunicodestring\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_wi.obj "%C%\042-wcsicmp\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_si.obj "%C%\043-stricmp\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_mi.obj "%C%\046-memicmp\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_wp.obj "%C%\035-wcspbrk\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_sp.obj "%C%\038-strpbrk\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foupcase.obj "%C%\008-rtlcompareunicodestring\upcase.c" >nul || goto :err
cl /nologo /O2 live_subst.c ref_wcslen.obj ref_memchr.obj ref_wcschr.obj ref_wcscmp.obj ref_rcm.obj ref_rcu.obj ref_upcasestr.obj upcase.obj wcslen.obj memchr.obj wcschr.obj wcscmp.obj rcm.obj rcu.obj upcasestr.obj wi.obj si.obj mi.obj wp.obj sp.obj ref_wi.obj ref_si.obj ref_mi.obj ref_wp.obj ref_sp.obj /Fe:live_subst.exe >nul || goto :err
"%H%live_subst.exe"
endlocal & exit /b 0
:err
echo BUILD ERROR & endlocal & exit /b 1
