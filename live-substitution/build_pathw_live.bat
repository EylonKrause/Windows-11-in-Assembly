@echo off
REM ===========================================================================
REM  Live substitution for the nine uncovered path manipulators, two DLLs:
REM    shlwapi     140 PathRemoveExtensionW   158 PathRenameExtensionW
REM                161 PathFindFileNameW      162 PathStripPathW
REM    kernelbase  143 PathCchFindExtension   144 PathCchRemoveExtension
REM                159 PathCchRenameExtension 160 PathCchAddExtension
REM                164 PathCchAddBackslash
REM  All nine are self-contained impl.asm, no helper .c, no init call.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fopw140.obj "%C%\140-pathremoveextensionw\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fopw158.obj "%C%\158-pathrenameextensionw\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fopw161.obj "%C%\161-pathfindfilenamew\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fopw162.obj "%C%\162-pathstrippathw\impl.asm"          >nul || goto :err
ml64 /nologo /c /Fopw143.obj "%C%\143-pathcchfindextension\impl.asm"    >nul || goto :err
ml64 /nologo /c /Fopw144.obj "%C%\144-pathcchremoveextension\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fopw159.obj "%C%\159-pathcchrenameextension\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fopw160.obj "%C%\160-pathcchaddextension\impl.asm"     >nul || goto :err
ml64 /nologo /c /Fopw164.obj "%C%\164-pathcchaddbackslash\impl.asm"     >nul || goto :err

cl /nologo /O2 live_subst_pathw.c pw140.obj pw158.obj pw161.obj pw162.obj ^
   pw143.obj pw144.obj pw159.obj pw160.obj pw164.obj shlwapi.lib ^
   /Fe:live_subst_pathw.exe >nul || goto :err

"%H%live_subst_pathw.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
