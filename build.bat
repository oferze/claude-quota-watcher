@echo off
rem Build Claude Quota Watcher. Run from "x64 Native Tools Command Prompt for VS".
rem   build.bat         build bin\ClaudeQuotaWatcher.exe (version from src\version.h)
rem   build.bat 1.2.3   build bin\ClaudeQuotaWatcher.exe as version 1.2.3
rem   build.bat test    build and run bin\tests.exe
rem   build.bat clean   delete bin and obj
setlocal
cd /d "%~dp0"

if /i "%~1"=="clean" (
  if exist bin rmdir /s /q bin
  if exist obj rmdir /s /q obj
  exit /b 0
)

rem A version parameter becomes APP_VERSION_MAJOR/MINOR/PATCH for both the compiler and the resource compiler.
set VERDEFS=
if not "%~1"=="" if /i not "%~1"=="test" (
  echo %~1| findstr /r /x "[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*" >nul || goto badversion
  echo %~1| findstr /r "^0[0-9] \.0[0-9]" >nul && goto badversion
  for /f "tokens=1-3 delims=." %%a in ("%~1") do (
    if %%a GTR 65535 goto badversion
    if %%b GTR 65535 goto badversion
    if %%c GTR 65535 goto badversion
    set VERDEFS=/DAPP_VERSION_MAJOR=%%a /DAPP_VERSION_MINOR=%%b /DAPP_VERSION_PATCH=%%c
  )
)

where cl >nul 2>nul
if errorlevel 1 (
  echo cl.exe not found - run from "x64 Native Tools Command Prompt for VS"
  exit /b 1
)

set CFLAGS=/nologo /std:c++17 /O2 /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX
set CORE=src\json.cpp src\usage.cpp src\credentials.cpp src\state.cpp src\popup_view.cpp
set NET=src\http.cpp src\poller.cpp
set UI=src\tray.cpp src\popup.cpp src\settings.cpp src\about.cpp
set LIBS=user32.lib gdi32.lib shell32.lib winhttp.lib comctl32.lib ole32.lib advapi32.lib dwmapi.lib shcore.lib

if not exist bin mkdir bin
if not exist obj mkdir obj

if /i "%~1"=="test" goto test

rc /nologo /c65001 %VERDEFS% /fo obj\app.res app.rc
if errorlevel 1 exit /b 1
cl %CFLAGS% %VERDEFS% /Fo:obj\ /Fe:bin\ClaudeQuotaWatcher.exe %CORE% %NET% %UI% src\main.cpp obj\app.res /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest %LIBS%
exit /b %ERRORLEVEL%

:test
cl %CFLAGS% /Isrc /Fo:obj\ /Fe:bin\tests.exe %CORE% %NET% %UI% tests\test_main.cpp /link /SUBSYSTEM:CONSOLE %LIBS%
if errorlevel 1 exit /b 1
bin\tests.exe %2
exit /b %ERRORLEVEL%

:badversion
echo Version must look like 1.2.3: three numbers from 0 to 65535, no leading zeros.
exit /b 1
