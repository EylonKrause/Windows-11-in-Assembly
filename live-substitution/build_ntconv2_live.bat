@echo off
REM ===========================================================================
REM  Live substitution for the eight remaining ntdll string converters:
REM    017 RtlDowncaseUnicodeString        018 RtlUnicodeStringToAnsiString
REM    019 RtlAnsiStringToUnicodeString    020 RtlUpcaseUnicodeStringToAnsiString
REM    024 RtlUnicodeStringToOemString     025 RtlOemStringToUnicodeString
REM    029 RtlOemToUnicodeN                165 RtlUpperString
REM  Six OS-built translation tables. 025 and 029 ship oem2umap.c BYTE-IDENTICALLY,
REM  so one object links for both.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fonc017.obj "%C%\017-rtldowncaseunicodestring\impl.asm"            >nul || goto :err
ml64 /nologo /c /Fonc018.obj "%C%\018-rtlunicodestringtoansistring\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fonc019.obj "%C%\019-rtlansistringtounicodestring\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fonc020.obj "%C%\020-rtlupcaseunicodestringtoansistring\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fonc024.obj "%C%\024-rtlunicodestringtooemstring\impl.asm"         >nul || goto :err
ml64 /nologo /c /Fonc025.obj "%C%\025-rtloemstringtounicodestring\impl.asm"         >nul || goto :err
ml64 /nologo /c /Fonc029.obj "%C%\029-rtloemtounicoden\impl.asm"                    >nul || goto :err
ml64 /nologo /c /Fonc165.obj "%C%\165-rtlupperstring\impl.asm"                      >nul || goto :err

cl /nologo /O2 /c /Fonct1.obj "%C%\017-rtldowncaseunicodestring\downcase.c"         >nul || goto :err
cl /nologo /O2 /c /Fonct2.obj "%C%\018-rtlunicodestringtoansistring\ansimap.c"      >nul || goto :err
cl /nologo /O2 /c /Fonct3.obj "%C%\019-rtlansistringtounicodestring\a2umap.c"       >nul || goto :err
cl /nologo /O2 /c /Fonct4.obj "%C%\020-rtlupcaseunicodestringtoansistring\upansimap.c" >nul || goto :err
cl /nologo /O2 /c /Fonct5.obj "%C%\024-rtlunicodestringtooemstring\oemmap.c"        >nul || goto :err
cl /nologo /O2 /c /Fonct6.obj "%C%\025-rtloemstringtounicodestring\oem2umap.c"      >nul || goto :err

cl /nologo /O2 live_subst_ntconv2.c nc017.obj nc018.obj nc019.obj nc020.obj ^
   nc024.obj nc025.obj nc029.obj nc165.obj ^
   nct1.obj nct2.obj nct3.obj nct4.obj nct5.obj nct6.obj ^
   /Fe:live_subst_ntconv2.exe >nul || goto :err

"%H%live_subst_ntconv2.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
