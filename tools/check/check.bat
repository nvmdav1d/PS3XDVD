@echo off
REM Host compile check with MSVC. Type-checks the sources against stub PSL1GHT
REM and SDL declarations; it does not generate PowerPC code.
setlocal

set VC="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VC% (
  echo Could not find vcvars64.bat - run this from a Developer Command Prompt.
  exit /b 1
)
call %VC% >nul || exit /b 1

set ROOT=%~dp0..\..
set OUT=%ROOT%\build\hostcheck
if not exist %OUT% mkdir %OUT%

set SOURCES= ^
  %ROOT%\source\main.c ^
  %ROOT%\source\util.c ^
  %ROOT%\source\gfx.c ^
  %ROOT%\source\input.c ^
  %ROOT%\source\xreg.c ^
  %ROOT%\source\storage.c ^
  %ROOT%\source\disc.c ^
  %ROOT%\source\rip.c ^
  %~dp0host_stubs.c

cl /nologo /c /W4 /wd4996 /wd4311 /std:c11 ^
   /I "%~dp0stubs" /I "%ROOT%\include" ^
   /Fo:"%OUT%\\" %SOURCES%

if errorlevel 1 (
  echo.
  echo HOST COMPILE CHECK FAILED
  exit /b 1
)

echo.
echo HOST COMPILE CHECK PASSED
exit /b 0