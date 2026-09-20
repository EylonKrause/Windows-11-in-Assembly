@echo off
REM ===========================================================================
REM  Live substitution for the six ntdll N-form converters:
REM    016 RtlUnicodeToUTF8N            021 RtlUnicodeToMultiByteN
REM    022 RtlMultiByteToUnicodeN       027 RtlUpcaseUnicodeToMultiByteN
REM    028 RtlUnicodeToOemN             031 RtlUpcaseUnicodeToOemN
REM
REM  Each of the five code-page converters brings its own translation table
REM  (ansimap / a2umap / upansimap / oemmap / upoemmap), and their symbols are
REM  all distinct, so unlike the rtlstr harness every one is linked. 016 needs
REM  no table.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fonc016.obj "%C%\016-rtlunicodetoutf8n\impl.asm"             >nul || goto :err
ml64 /nologo /c /Fonc021.obj "%C%\021-rtlunicodetomultibyten\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fonc022.obj "%C%\022-rtlmultibytetounicoden\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fonc027.obj "%C%\027-rtlupcaseunicodetomultibyten\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fonc028.obj "%C%\028-rtlunicodetooemn\impl.asm"              >nul || goto :err
ml64 /nologo /c /Fonc031.obj "%C%\031-rtlupcaseunicodetooemn\impl.asm"        >nul || goto :err

cl /nologo /O2 /c /Foncm021.obj "%C%\021-rtlunicodetomultibyten\ansimap.c"        >nul || goto :err
cl /nologo /O2 /c /Foncm022.obj "%C%\022-rtlmultibytetounicoden\a2umap.c"         >nul || goto :err
cl /nologo /O2 /c /Foncm027.obj "%C%\027-rtlupcaseunicodetomultibyten\upansimap.c" >nul || goto :err
cl /nologo /O2 /c /Foncm028.obj "%C%\028-rtlunicodetooemn\oemmap.c"               >nul || goto :err
cl /nologo /O2 /c /Foncm031.obj "%C%\031-rtlupcaseunicodetooemn\upoemmap.c"       >nul || goto :err

cl /nologo /O2 live_subst_ntconv.c nc016.obj nc021.obj nc022.obj nc027.obj nc028.obj nc031.obj ^
   ncm021.obj ncm022.obj ncm027.obj ncm028.obj ncm031.obj /Fe:live_subst_ntconv.exe >nul || goto :err

"%H%live_subst_ntconv.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
