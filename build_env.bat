@echo off
cd /d "%~dp0"
if exist build rmdir /s /q build

set MSYS_PATH=C:\msys64\usr\bin
if not exist %MSYS_PATH%\bash.exe set MSYS_PATH=C:\msys64.bak\usr\bin
if not exist %MSYS_PATH%\bash.exe (
    echo ERROR: Cannot find MSYS2 installation.
    echo Please install MSYS2 or run build_env.bat from an MSYS2 terminal.
    pause
    exit /b 1
)

set UCRT_BIN=C:\msys64\ucrt64\bin
if not exist %UCRT_BIN%\cmake.exe set UCRT_BIN=C:\msys64.bak\ucrt64\bin

echo Using MSYS2: %MSYS_PATH%
echo Using toolchain: %UCRT_BIN%

rem Convert current directory path for MSYS2 bash
set CURRENT_DIR=%~dp0
set CURRENT_DIR=%CURRENT_DIR:\=/%
set CURRENT_DIR=%CURRENT_DIR:C:=/c%
set CURRENT_DIR=%CURRENT_DIR:c:=/c%
rem Remove trailing slash if present
if "%CURRENT_DIR:~-1%"=="/" set CURRENT_DIR=%CURRENT_DIR:~0,-1%

rem Run cmake through MSYS2 bash to get proper environment
"%MSYS_PATH%\bash.exe" -c "export PATH='%UCRT_BIN:/=\%:/usr/bin:$PATH'; export MINGW_PREFIX='%UCRT_BIN:/=\%'; cmake -S '%CURRENT_DIR%' -B '%CURRENT_DIR%/build' -G Ninja"

if errorlevel 1 (
    echo CMake configure failed.
    pause
    exit /b 1
)

"%MSYS_PATH%\bash.exe" -c "export PATH='%UCRT_BIN:/=\%:/usr/bin:$PATH'; export MINGW_PREFIX='%UCRT_BIN:/=\%'; cmake --build '%CURRENT_DIR%/build'"

if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

echo.
echo Build complete. Executable in build\bin\
pause
