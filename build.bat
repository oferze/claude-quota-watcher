@echo off
rem Build Claude Quota Watcher. Run from "x64 Native Tools Command Prompt for VS".
rem   build.bat         build bin\ClaudeQuotaWatcher.exe
rem   build.bat test    build and run bin\tests.exe
rem   build.bat clean   delete bin and obj
setlocal
cd /d "%~dp0"

if /i "%~1"=="clean" (
  if exist bin rmdir /s /q bin
  if exist obj rmdir /s /q obj
  exit /b 0
)

where cl >nul 2>nul
if errorlevel 1 (
  echo cl.exe not found - run from "x64 Native Tools Command Prompt for VS"
  exit /b 1
)

set CFLAGS=/nologo /std:c++17 /O2 /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX
set CORE=src\json.cpp src\usage.cpp src\credentials.cpp src\state.cpp src\popup_view.cpp
set NET=src\http.cpp src\poller.cpp
set UI=src\tray.cpp src\popup.cpp
set LIBS=user32.lib gdi32.lib shell32.lib winhttp.lib comctl32.lib ole32.lib advapi32.lib dwmapi.lib shcore.lib

if not exist bin mkdir bin
if not exist obj mkdir obj

if /i "%~1"=="test" goto test

cl %CFLAGS% /Fo:obj\ /Fe:bin\ClaudeQuotaWatcher.exe %CORE% %NET% %UI% src\main.cpp /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest %LIBS%
exit /b %ERRORLEVEL%

:test
cl %CFLAGS% /Isrc /Fo:obj\ /Fe:bin\tests.exe %CORE% %NET% %UI% tests\test_main.cpp /link /SUBSYSTEM:CONSOLE %LIBS%
if errorlevel 1 exit /b 1
bin\tests.exe %2
exit /b %ERRORLEVEL%
