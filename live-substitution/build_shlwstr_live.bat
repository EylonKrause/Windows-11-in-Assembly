@echo off
REM ===========================================================================
REM  Live substitution for the seven uncovered shlwapi string scanners:
REM    131 StrChrW    133 StrStrW          134 StrRChrW   136 StrCSpnW
REM    137 StrPBrkW   138 PathIsFileSpecW  139 StrTrimW
REM  All seven are self-contained impl.asm, no helper .c, no init call.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fosw131.obj "%C%\131-strchrw\impl.asm"          >nul || goto :err
ml64 /nologo /c /Fosw133.obj "%C%\133-strstrw\impl.asm"          >nul || goto :err
ml64 /nologo /c /Fosw134.obj "%C%\134-strrchrw\impl.asm"         >nul || goto :err
ml64 /nologo /c /Fosw136.obj "%C%\136-strcspnw\impl.asm"         >nul || goto :err
ml64 /nologo /c /Fosw137.obj "%C%\137-strpbrkw\impl.asm"         >nul || goto :err
ml64 /nologo /c /Fosw138.obj "%C%\138-pathisfilespecw\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fosw139.obj "%C%\139-strtrimw\impl.asm"         >nul || goto :err

cl /nologo /O2 live_subst_shlwstr.c sw131.obj sw133.obj sw134.obj sw136.obj ^
   sw137.obj sw138.obj sw139.obj shlwapi.lib /Fe:live_subst_shlwstr.exe >nul || goto :err

"%H%live_subst_shlwstr.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
