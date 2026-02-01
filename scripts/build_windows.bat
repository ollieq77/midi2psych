@echo off
echo ========================================
echo  MIDI2Psych Builder for Windows
echo  Using MinGW-w64 (No VS Required!)
echo ========================================
echo.

:: Check if g++ is available
where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: g++ not found!
    echo.
    echo Please install MinGW-w64:
    echo 1. Download from: https://winlibs.com/
    echo 2. Extract to C:\mingw64
    echo 3. Add C:\mingw64\bin to your PATH
    echo.
    echo Or use: winget install -e --id GnuWin32.Make
    pause
    exit /b 1
)

echo Compiler found: 
g++ --version | findstr /C:"g++"
echo.

echo Building optimized executable...
echo.

:: Ensure dist folder exists (repo root)
:: Determine script location and repo root so script works from any cwd
set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\
set OUT_DIR=%REPO_ROOT%dist
set SRC_FILE=%REPO_ROOT%midi2psych.cpp

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: Build with maximum optimization; source is in repo root
g++ -std=c++17 ^
    -O3 ^
    -march=native ^
    -flto ^
    -s ^
    -DNDEBUG ^
    -o "%OUT_DIR%\midi2psych.exe" ^
    "%SRC_FILE%" ^
    -lcomctl32 -lcomdlg32 -lgdi32 -lshell32

:: Copy over LICENCE.rtf and README.html into the dist folder in subfolder DOCUMENTS
set DOCS_DIR=%OUT_DIR%\DOCUMENTS
if not exist "%DOCS_DIR%" mkdir "%DOCS_DIR%"
copy "%REPO_ROOT%LICENSE.rtf" "%DOCS_DIR%\LICENSE.rtf"
copy "%REPO_ROOT%README.html" "%DOCS_DIR%\README.html"

if %ERRORLEVEL% equ 0 (
    echo.
    echo ========================================
    echo  BUILD SUCCESSFUL!
    echo ========================================
    echo.
    echo Executable created: %OUT_DIR%\midi2psych.exe
    echo File size: 
    for %%A in ("%OUT_DIR%\midi2psych.exe") do echo   %%~zA bytes
    echo.
    echo Test it with:
    echo   %OUT_DIR%\midi2psych.exe -h
    echo.
) else (
    echo.
    echo ========================================
    echo  BUILD FAILED!
    echo ========================================
    pause
    exit /b 1
)

pause
