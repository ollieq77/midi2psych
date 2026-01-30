@echo off
setlocal enabledelayedexpansion

:: MIDI2Psych - One-Click Build Script
:: Checks for compiler and builds automatically

echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║     MIDI2Psych - Automatic Builder                    ║
echo ║     No Visual Studio Required!                         ║
echo ╚════════════════════════════════════════════════════════╝
echo.

:: Function to check if command exists
where g++ >nul 2>&1
if %ERRORLEVEL% equ 0 goto :compile

echo [!] g++ compiler not found!
echo.
echo Let me help you install it...
echo.
echo Select installation method:
echo   [1] Automatic (winget) - Recommended
echo   [2] Manual download instructions
echo   [Q] Quit
echo.
set /p choice="Enter choice (1/2/Q): "

if /i "%choice%"=="1" goto :install_winget
if /i "%choice%"=="2" goto :install_manual
if /i "%choice%"=="Q" exit /b
goto :compile

:install_winget
echo.
echo Installing MinGW via winget...
winget install -e --id GnuWin32.Make
if %ERRORLEVEL% equ 0 (
    echo.
    echo Installation complete! Please restart this script.
    pause
    exit /b
) else (
    echo.
    echo Winget installation failed. Trying manual method...
    goto :install_manual
)

:install_manual
echo.
echo Manual Installation Steps:
echo.
echo 1. Open: https://winlibs.com/
echo 2. Download: Latest UCRT runtime (first download link)
echo 3. Extract to: C:\mingw64
echo 4. Add to PATH:
echo    - Search "Environment Variables" in Windows
echo    - Edit "Path" variable
echo    - Add: C:\mingw64\bin
echo    - Click OK on all dialogs
echo 5. Restart this script
echo.
echo Press any key to open the download page...
pause >nul
start https://winlibs.com/
exit /b

:compile
echo [✓] Compiler found!
g++ --version | findstr "g++"
echo.
echo Building optimized executable...
echo.

set "start_time=%time%"

:: Compile with aggressive optimizations
g++ -std=c++17 ^
    -O3 ^
    -march=native ^
    -flto ^
    -s ^
    -DNDEBUG ^
    -static-libgcc ^
    -static-libstdc++ ^
    -o midi2psych.exe ^
    midi2psych.cpp

if %ERRORLEVEL% neq 0 (
    echo.
    echo [✗] BUILD FAILED!
    echo Check that midi2psych.cpp is in the same folder.
    pause
    exit /b 1
)

:: Calculate build time
set "end_time=%time%"

echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║               BUILD SUCCESSFUL!                        ║
echo ╚════════════════════════════════════════════════════════╝
echo.

:: Display file info
echo File: midi2psych.exe
for %%A in (midi2psych.exe) do (
    set size=%%~zA
    set /a size_kb=!size! / 1024
    echo Size: !size_kb! KB
)
echo.

:: Test the executable
echo Testing executable...
midi2psych.exe -h >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [✓] Executable works correctly!
) else (
    echo [!] Warning: Executable test failed
)

echo.
echo Quick test:
echo   midi2psych.exe -h
echo.
echo Full usage:
echo   midi2psych.exe player1.mid player2.mid output.json
echo.
echo See README.md for detailed documentation!
echo.

:: Offer to run test
set /p runtest="Run help to see all options? (Y/n): "
if /i "!runtest!"=="Y" (
    echo.
    midi2psych.exe -h
)
if /i "!runtest!"=="" (
    echo.
    midi2psych.exe -h
)

echo.
pause
