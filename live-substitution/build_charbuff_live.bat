@echo off
REM ===========================================================================
Rem  live-run proof for change 277 (user32!CharUpperBuffW + CharLowerBuffW).
REM
Rem  both exports are patched at once: they are the same loop with two tables and
REM  two ranges, and discovery/rtl_integer_char.c measured the lower form at
REM  1.16 ns per character against the upper one's 0.78, so they are not even the
REM  same code underneath. A proof that patched only one would be a proof about
REM  half a change.
REM
REM  Every case compares the return value AND every character of the buffer --
Rem  including the characters past the count. These exports take a count, not a
REM  terminator, so an implementation that rounded the count up to a whole vector
REM  block would corrupt what follows, which is change 016's defect exactly and
REM  which only a whole-buffer comparison sees.
REM
REM  tables.c is linked in and its tables are built BEFORE the patch exists: they
REM  build themselves by asking these very exports 65536 questions each, and under
REM  the patch they would be asking our code what our code should say.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write copy
REM  of user32. No system process is touched, nothing on disk is modified, and
REM  both prologues are restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Focharbuff.obj "%C%\277-charupperbuffw\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Focharbufftb.obj "%C%\277-charupperbuffw\tables.c" >nul || goto :err
cl /nologo /O2 live_subst_charbuff.c charbuff.obj charbufftb.obj user32.lib /Fe:live_subst_charbuff.exe >nul || goto :err
"%H%live_subst_charbuff.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
