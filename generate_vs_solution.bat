@echo off
setlocal
rem ---------------------------------------------------------------------------
rem  AquaLens - generate a Visual Studio solution (build\AquaLens.sln)
rem  Requires: Visual Studio 2022 or 2026 with "Desktop development with C++",
rem            Qt 6.4+ for MSVC 64-bit, including the Qt Charts module.
rem  Optional: set QTDIR=C:\Qt\6.x.x\msvc2022_64 before running.
rem ---------------------------------------------------------------------------
cd /d "%~dp0"

rem ---- locate Qt --------------------------------------------------------------
if defined QTDIR goto :have_qt
for /d %%D in (C:\Qt\6.*) do if exist "%%D\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" set "QTDIR=%%D\msvc2022_64"
if defined QTDIR goto :have_qt
echo [ERROR] Qt 6 for MSVC was not found under C:\Qt.
echo         Install "MSVC 2022 64-bit" + "Qt Charts" with the Qt Online Installer,
echo         or run:  set QTDIR=C:\path\to\Qt\6.x.x\msvc2022_64
pause
exit /b 1
:have_qt
echo Using Qt: %QTDIR%

rem ---- locate CMake (PATH first, then the copy bundled with Visual Studio) -----
where cmake >nul 2>nul
if not errorlevel 1 goto :have_cmake
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_cmake
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%I"
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if not exist "%VSCMAKE%\cmake.exe" goto :no_cmake
set "PATH=%VSCMAKE%;%PATH%"
goto :have_cmake
:no_cmake
echo [ERROR] CMake not found. In the Visual Studio Installer, add "C++ CMake tools for Windows".
pause
exit /b 1
:have_cmake

rem ---- generate (newest Visual Studio first) ---------------------------------
if exist build rmdir /s /q build
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="%QTDIR%" >nul 2>nul
if not errorlevel 1 goto :done
if exist build rmdir /s /q build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QTDIR%"
if errorlevel 1 (
    echo [ERROR] CMake could not generate the solution. See the messages above.
    pause
    exit /b 1
)
:done
echo.
echo  Solution created: build\AquaLens.sln
echo  In Visual Studio press F5 to build and run.
echo.
start "" "build\AquaLens.sln"
