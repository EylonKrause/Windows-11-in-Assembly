@echo off
REM ===========================================================================
REM  Live substitution for CryptBinaryToStringA and CryptBinaryToStringW --
Rem  eight changes behind two exports, dispatched by dwFlags:
REM
REM    CRYPT_STRING_BASE64        081 (A)  083 (W)
REM    CRYPT_STRING_BASE64HEADER  092 (A)  093 (W)
REM    CRYPT_STRING_HEX           090 (A)  091 (W)
REM    CRYPT_STRING_HEXRAW        085 (A)  087 (W)
REM
REM  None of the eight is the export: the thing patched over it is a dispatcher
REM  in live_subst_b2s.c. An unhandled format reaches a TRAP stub rather than the
REM  real export, because the real export is what the dispatcher is patched over
REM  and calling it would recurse.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fob081.obj "%C%\081-cryptbinarytostring-base64\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fob083.obj "%C%\083-cryptbinarytostringw-base64\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fob085.obj "%C%\085-cryptbinarytostring-hexraw\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fob087.obj "%C%\087-cryptbinarytostringw-hexraw\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fob090.obj "%C%\090-cryptbinarytostring-hexfmt\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fob091.obj "%C%\091-cryptbinarytostringw-hexfmt\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fob092.obj "%C%\092-cryptbinarytostring-base64header\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fob093.obj "%C%\093-cryptbinarytostringw-base64header\impl.asm" >nul || goto :err

cl /nologo /O2 live_subst_b2s.c b081.obj b083.obj b085.obj b087.obj b090.obj b091.obj ^
   b092.obj b093.obj crypt32.lib /Fe:live_subst_b2s.exe >nul || goto :err

"%H%live_subst_b2s.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
