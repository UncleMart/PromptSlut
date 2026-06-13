@echo off
cd /d "%~dp0"

echo [Backup] Backing up configuration and memory files if present...
if not exist temp_backup mkdir temp_backup
if exist build\bin\*.profile copy build\bin\*.profile temp_backup\ /y >nul
if exist build\bin\*.key copy build\bin\*.key temp_backup\ /y >nul
if exist build\bin\*.dict copy build\bin\*.dict temp_backup\ /y >nul
if exist build\bin\*.prompt copy build\bin\*.prompt temp_backup\ /y >nul
if exist build\bin\system_prompt*.txt copy build\bin\system_prompt*.txt temp_backup\ /y >nul
if exist build\bin\sessions xcopy build\bin\sessions temp_backup\sessions\ /e /y /i >nul
if exist build\bin\memory xcopy build\bin\memory temp_backup\memory\ /e /y /i >nul

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

echo [Restore] Restoring configuration and memory files from backup...
if exist temp_backup\*.profile copy temp_backup\*.profile build\bin\ /y >nul
if exist temp_backup\*.key copy temp_backup\*.key build\bin\ /y >nul
if exist temp_backup\*.dict copy temp_backup\*.dict build\bin\ /y >nul
if exist temp_backup\*.prompt copy temp_backup\*.prompt build\bin\ /y >nul
if exist temp_backup\system_prompt*.txt copy temp_backup\system_prompt*.txt build\bin\ /y >nul
if exist temp_backup\sessions xcopy temp_backup\sessions build\bin\sessions\ /e /y /i >nul
if exist temp_backup\memory xcopy temp_backup\memory build\bin\memory\ /e /y /i >nul
if exist temp_backup rmdir /s /q temp_backup

pause
