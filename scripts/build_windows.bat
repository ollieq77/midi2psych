@echo off
setlocal EnableDelayedExpansion
title MIDI2Psych Builder

:: ============================================================
::  MIDI2Psych Builder for Windows  [MinGW-w64 / No VS needed]
::  Customize the CONFIG section below to suit your setup.
:: ============================================================

:: -- CONFIG --------------------------------------------------
set BUILD_TYPE=debug
:: Options: release | debug | asan
::   release  -> -O3, -march=native, -flto, stripped binary
::   debug    -> -O0, -g, full symbols, assertions on
::   asan     -> -O1, -g, AddressSanitizer (MinGW support varies)

set WARN_LEVEL=normal
:: Options: minimal | normal | strict
::   minimal  -> -w (suppress all warnings)
::   normal   -> -Wall -Wextra
::   strict   -> -Wall -Wextra -Wpedantic -Werror

set STD=c++17
:: Options: c++14 | c++17 | c++20 | c++23

set EXTRA_FLAGS=
:: Add any custom flags here, e.g.: -DFEATURE_X=1 -funroll-loops
:: ------------------------------------------------------------

cls
echo.
echo  ==========================================
echo   MIDI2Psych Builder  ^|  MinGW-w64
echo   No Visual Studio required
echo  ==========================================
echo.

:: -- Timestamp (PowerShell, works on Win 10/11) --------------
for /f "usebackq delims=" %%T in (`powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd HH:mm:ss'"`) do set BUILD_TIME=%%T

echo  >> Build started : %BUILD_TIME%
echo  >> Profile       : %BUILD_TYPE% ^| std=%STD% ^| warnings=%WARN_LEVEL%
echo.

:: -- Check for g++ -------------------------------------------
where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    call :print_error "g++ not found on PATH"
    echo.
    echo  Fix options:
    echo    1. Download MinGW-w64 from https://winlibs.com/
    echo    2. Extract to C:\mingw64
    echo    3. Add C:\mingw64\bin to your PATH
    echo    4. Restart this terminal and try again
    echo.
    echo  Quick install via winget:
    echo    winget install -e --id MSYS2.MSYS2
    echo.
    pause
    exit /b 1
)

for /f "tokens=1,2,3" %%A in ('g++ --version 2^>^&1') do (
    set COMPILER_VER=%%A %%B %%C
    goto :compiler_found
)
:compiler_found
echo  >> Compiler : !COMPILER_VER!
echo.

:: -- Resolve paths -------------------------------------------
set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\
set OUT_DIR=%REPO_ROOT%dist
set SRC_DIR=%REPO_ROOT%src
set INCLUDE_DIR=%REPO_ROOT%include

:: -- Verify required source files exist ----------------------
echo  [*] Checking source files...
set MISSING_FILES=0
for %%F in (main.cpp midi_parser.cpp psych_converter.cpp gui.cpp gui_logger.cpp progress_bar.cpp) do (
    if not exist "%SRC_DIR%\%%F" (
        call :print_warn "Missing: src\%%F"
        set /a MISSING_FILES+=1
    ) else (
        echo    [OK] src\%%F
    )
)
if !MISSING_FILES! gtr 0 (
    echo.
    call :print_error "!MISSING_FILES! source file(s) missing -- aborting"
    pause
    exit /b 1
)
echo.

:: -- Create output dir ---------------------------------------
if not exist "%OUT_DIR%" (
    mkdir "%OUT_DIR%"
    echo  [*] Created output dir: %OUT_DIR%
    echo.
)

:: -- Build flags by profile ----------------------------------
if /i "%BUILD_TYPE%"=="release" (
    set OPT_FLAGS=-O3 -march=native -flto -s -DNDEBUG
    set OUT_NAME=midi2psych.exe
) else if /i "%BUILD_TYPE%"=="debug" (
    set OPT_FLAGS=-O0 -g -DDEBUG
    set OUT_NAME=midi2psych_debug.exe
) else if /i "%BUILD_TYPE%"=="asan" (
    set OPT_FLAGS=-O1 -g -fsanitize=address,undefined
    set OUT_NAME=midi2psych_asan.exe
) else (
    call :print_error "Unknown BUILD_TYPE: %BUILD_TYPE% -- use release, debug, or asan"
    pause
    exit /b 1
)

:: -- Warning flags --------------------------------------------
if /i "%WARN_LEVEL%"=="minimal" (
    set WARN_FLAGS=-w
) else if /i "%WARN_LEVEL%"=="normal" (
    set WARN_FLAGS=-Wall -Wextra
) else if /i "%WARN_LEVEL%"=="strict" (
    set WARN_FLAGS=-Wall -Wextra -Wpedantic -Werror
) else (
    call :print_error "Unknown WARN_LEVEL: %WARN_LEVEL% -- use minimal, normal, or strict"
    pause
    exit /b 1
)

:: -- Compile -------------------------------------------------
echo  [*] Building [%BUILD_TYPE%] -> %OUT_NAME%
echo  ------------------------------------------
echo.

g++ -std=%STD% ^
    %OPT_FLAGS% ^
    %WARN_FLAGS% ^
    %EXTRA_FLAGS% ^
    -I"%INCLUDE_DIR%" ^
    -mwindows ^
    -o "%OUT_DIR%\%OUT_NAME%" ^
    "%SRC_DIR%\main.cpp" ^
    "%SRC_DIR%\midi_parser.cpp" ^
    "%SRC_DIR%\psych_converter.cpp" ^
    "%SRC_DIR%\gui.cpp" ^
    "%SRC_DIR%\gui_logger.cpp" ^
    "%SRC_DIR%\progress_bar.cpp" ^
    -lcomctl32 -lcomdlg32 -lgdi32 -lshell32 2>&1

set BUILD_RESULT=%ERRORLEVEL%

:: -- Result --------------------------------------------------
echo.
if %BUILD_RESULT% equ 0 (
    for %%A in ("%OUT_DIR%\%OUT_NAME%") do (
        set FILE_SIZE=%%~zA
        set FILE_PATH=%%~fA
    )
    set /a FILE_SIZE_KB=!FILE_SIZE! / 1024

    echo  ==========================================
    echo   BUILD SUCCESSFUL
    echo  ==========================================
    echo.
    echo  Output  : !FILE_PATH!
    echo  Size    : !FILE_SIZE! bytes  (~!FILE_SIZE_KB! KB^)
    echo  Profile : %BUILD_TYPE%
    echo.
    echo  Test it:
    echo    "%OUT_DIR%\%OUT_NAME%" -h
    echo.
    pause
    exit /b 0
)

echo  ==========================================
echo   BUILD FAILED
echo  ==========================================
echo.
echo  [!!] Exit code : %BUILD_RESULT%
echo.
echo  Common causes:
echo    - Syntax error in your source files (check above)
echo    - Missing #include or mismatched header
echo    - -march=native unsupported on your CPU (try removing it)
echo    - -flto issues: remove it from OPT_FLAGS in this script
echo.
pause
exit /b %BUILD_RESULT%

:: -- Helpers -------------------------------------------------
:print_error
echo  [!!] ERROR: %~1
exit /b 0

:print_warn
echo  [!!] WARN:  %~1
exit /b 0