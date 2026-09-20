@echo off
REM ===========================================================================
REM  Live substitution for CryptStringToBinaryA and CryptStringToBinaryW --
REM  the DECODE half of the crypt32 pair, eight changes behind two exports:
REM
REM    CRYPT_STRING_BASE64        082 (A)  084 (W)
REM    CRYPT_STRING_BASE64HEADER  104 (A)  105 (W)
REM    CRYPT_STRING_BASE64_ANY    106 (A)  107 (W)
REM    CRYPT_STRING_HEXRAW        086 (A)  088 (W)
REM
REM  revtab.c is linked ONCE (082/084/104/106 ship the identical file) and
REM  hexrev.c ONCE (086 and 088 differ only in a path comment). Both define
REM  wia_b64rev/wia_hexrev, so a second copy is a duplicate-symbol error.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Fos082.obj "%C%\082-cryptstringtobinary-base64\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fos084.obj "%C%\084-cryptstringtobinaryw-base64\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fos086.obj "%C%\086-cryptstringtobinary-hexraw\impl.asm"        >nul || goto :err
ml64 /nologo /c /Fos088.obj "%C%\088-cryptstringtobinaryw-hexraw\impl.asm"       >nul || goto :err
ml64 /nologo /c /Fos104.obj "%C%\104-cryptstringtobinary-base64header\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fos105.obj "%C%\105-cryptstringtobinaryw-base64header\impl.asm" >nul || goto :err
ml64 /nologo /c /Fos106.obj "%C%\106-cryptstringtobinary-base64any\impl.asm"     >nul || goto :err
ml64 /nologo /c /Fos107.obj "%C%\107-cryptstringtobinaryw-base64any\impl.asm"    >nul || goto :err

cl /nologo /O2 /c /Fosrev.obj "%C%\082-cryptstringtobinary-base64\revtab.c" >nul || goto :err
cl /nologo /O2 /c /Foshex.obj "%C%\086-cryptstringtobinary-hexraw\hexrev.c" >nul || goto :err

cl /nologo /O2 live_subst_s2b.c s082.obj s084.obj s086.obj s088.obj s104.obj s105.obj ^
   s106.obj s107.obj srev.obj shex.obj crypt32.lib /Fe:live_subst_s2b.exe >nul || goto :err

"%H%live_subst_s2b.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
