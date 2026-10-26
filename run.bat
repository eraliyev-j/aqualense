@echo off
setlocal
rem ---------------------------------------------------------------------------
rem  AquaLens - build and run with the MinGW toolchain shipped with Qt.
rem  Usage (VS Code terminal):  .\run.bat
rem  Optional: set QTDIR=C:\Qt\6.x.x\mingw_64 before running.
rem ---------------------------------------------------------------------------
cd /d "%~dp0"

rem ---- Qt (MinGW build) -------------------------------------------------------
if defined QTDIR goto :have_qt
for /d %%D in (C:\Qt\6.*) do if exist "%%D\mingw_64\lib\cmake\Qt6\Qt6Config.cmake" set "QTDIR=%%D\mingw_64"
if defined QTDIR goto :have_qt
echo [ERROR] Qt 6 for MinGW was not found. Folders in C:\Qt:
dir /b C:\Qt
echo Open Qt Maintenance Tool and add: Qt 6.x - "MinGW 64-bit" and "Qt Charts".
goto :fail
:have_qt

rem ---- MinGW compiler ---------------------------------------------------------
set "MINGW="
for /d %%D in (C:\Qt\Tools\mingw*_64) do if exist "%%D\bin\g++.exe" set "MINGW=%%D"
if defined MINGW goto :have_mingw
echo [ERROR] MinGW compiler not found in C:\Qt\Tools.
echo Open Qt Maintenance Tool - Developer and Designer Tools - add "MinGW 64-bit".
goto :fail
:have_mingw

set "PATH=%MINGW%\bin;%QTDIR%\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"

where cmake >nul 2>nul
if not errorlevel 1 goto :have_cmake
echo [ERROR] CMake not found. Add "CMake" in Qt Maintenance Tool - Developer and Designer Tools.
goto :fail
:have_cmake

set "GEN=MinGW Makefiles"
where ninja >nul 2>nul
if not errorlevel 1 set "GEN=Ninja"

echo.
echo   Qt       : %QTDIR%
echo   Compiler : %MINGW%
echo   Generator: %GEN%
echo.

rem ---- configure + build ------------------------------------------------------
cmake -S . -B build-mingw -G "%GEN%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTDIR%"
if errorlevel 1 goto :fail
cmake --build build-mingw --parallel
if errorlevel 1 goto :fail

echo.
echo   Build OK - starting AquaLens...
start "" "build-mingw\AquaLens.exe"
exit /b 0

:fail
echo.
echo   Build failed. Copy the messages above and send them.
pause
exit /b 1
