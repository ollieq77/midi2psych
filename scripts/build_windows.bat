@echo off
setlocal EnableExtensions
title MIDI2Psych Builder

:: ============================================================
::  MIDI2Psych Builder  |  MinGW-w64  |  No Visual Studio
:: ============================================================
::
::  CONFIGURATION -- edit these, nothing else needs touching
::
::    BUILD_TYPE   release | debug | asan
::    WARN_LEVEL   minimal | normal | strict
::    STD          c++14 | c++17 | c++20 | c++23
::    BUNDLE_DLLS  space-separated DLL filenames from MinGW bin
::                 release mode statically links libgcc + libstdc++
::                 so only libwinpthread-1.dll is usually needed
::    COPY_ASSETS  1 = copy assets\ folder into release, 0 = skip
::    MAKE_ZIP     1 = zip up the release folder, 0 = skip
::
:: ============================================================

set BUILD_TYPE=release
set WARN_LEVEL=normal
set STD=c++17
set EXTRA_FLAGS=
set BUNDLE_DLLS=libwinpthread-1.dll
set COPY_ASSETS=1
set MAKE_ZIP=1

:: ============================================================
::  Nothing below this line should need editing
:: ============================================================

cls
echo.
echo  =========================================
echo   MIDI2Psych Builder  ^|  MinGW-w64
echo  =========================================
echo.

:: Timestamp (no delayed expansion needed here)
for /f "usebackq delims=" %%T in (
    `powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd HH:mm:ss'"`
) do set BUILD_TIME=%%T

echo  Started  : %BUILD_TIME%
echo  Profile  : %BUILD_TYPE%  /  std=%STD%  /  warnings=%WARN_LEVEL%
echo.

:: ---- Locate g++ ----
where g++ >nul 2>&1
if errorlevel 1 (
    echo  [FAIL] g++ not found on PATH.
    echo.
    echo         Install MinGW-w64 via one of:
    echo           WinLibs  --  https://winlibs.com/
    echo           MSYS2    --  winget install -e --id MSYS2.MSYS2
    echo.
    echo         Then add its bin folder to PATH and reopen this terminal.
    echo.
    pause & exit /b 1
)

for /f "delims=" %%P in ('where g++ 2^>nul') do (
    set GPP_EXE=%%P
    goto :found_gpp
)
:found_gpp

for %%D in ("%GPP_EXE%") do set MINGW_BIN=%%~dpD
:: strip trailing backslash
if "%MINGW_BIN:~-1%"=="\" set MINGW_BIN=%MINGW_BIN:~0,-1%

for /f "delims=" %%V in ('g++ --version 2^>nul') do (
    set COMPILER_VER=%%V
    goto :found_ver
)
:found_ver

echo  Compiler : %COMPILER_VER%
echo  MinGW    : %MINGW_BIN%
echo.

:: ---- Paths ----
set SCRIPT_DIR=%~dp0
for %%R in ("%SCRIPT_DIR%..") do set REPO_ROOT=%%~fR

set SRC=%REPO_ROOT%\src
set INC=%REPO_ROOT%\include
set ASSETS=%REPO_ROOT%\assets
set BUILD=%REPO_ROOT%\dist\build
set RELEASE=%REPO_ROOT%\dist\release
set ZIPS=%REPO_ROOT%\dist\zips

:: ---- Clean stray files left by the old script ----
echo  [....] Cleaning stray files
set _FOUND=no
for %%F in (
    "%REPO_ROOT%\Build"          "%REPO_ROOT%\Compiler"
    "%REPO_ROOT%\Profile"        "%REPO_ROOT%\MinGW"
    "%REPO_ROOT%\g++"            "%REPO_ROOT%\midi2psych.exe"
    "%REPO_ROOT%\midi2psych_debug.exe"  "%REPO_ROOT%\midi2psych_asan.exe"
    "%SCRIPT_DIR%Build"          "%SCRIPT_DIR%Compiler"
    "%SCRIPT_DIR%Profile"        "%SCRIPT_DIR%midi2psych.exe"
    "%SCRIPT_DIR%midi2psych_debug.exe"  "%SCRIPT_DIR%midi2psych_asan.exe"
    "%REPO_ROOT%\dist\midi2psych.exe"
    "%REPO_ROOT%\dist\midi2psych_debug.exe"
    "%REPO_ROOT%\dist\midi2psych_asan.exe"
) do (
    if exist %%F (
        del /f /q %%F >nul 2>&1
        echo         Removed: %%~F
        set _FOUND=yes
    )
)
if "%_FOUND%"=="no" echo         Nothing to remove
echo.

:: ---- Check source files ----
echo  [....] Checking source files
set SRC_OK=yes
for %%F in (
    main.cpp  midi_parser.cpp  psych_converter.cpp
    gui.cpp   gui_logger.cpp   progress_bar.cpp
) do (
    if exist "%SRC%\%%F" (
        echo         OK  %%F
    ) else (
        echo         MISSING  %%F
        set SRC_OK=no
    )
)

if "%SRC_OK%"=="no" (
    echo.
    echo  [FAIL] One or more source files are missing. Cannot continue.
    echo.
    pause & exit /b 1
)
echo.

:: ---- Create output dirs ----
if not exist "%BUILD%"   mkdir "%BUILD%"   >nul 2>&1
if not exist "%RELEASE%" mkdir "%RELEASE%" >nul 2>&1
if not exist "%ZIPS%"    mkdir "%ZIPS%"    >nul 2>&1

:: ---- Resolve build flags ----
if /i "%BUILD_TYPE%"=="release" (
    set OPT=-O3 -march=native -flto -s -DNDEBUG -static-libgcc -static-libstdc++
    set OUT=midi2psych.exe
) else if /i "%BUILD_TYPE%"=="debug" (
    set OPT=-O0 -g -DDEBUG
    set OUT=midi2psych_debug.exe
) else if /i "%BUILD_TYPE%"=="asan" (
    set OPT=-O1 -g -fsanitize=address,undefined
    set OUT=midi2psych_asan.exe
) else (
    echo  [FAIL] Unknown BUILD_TYPE "%BUILD_TYPE%" -- use release, debug, or asan
    pause & exit /b 1
)

if /i "%WARN_LEVEL%"=="minimal" (
    set WARN=-w
) else if /i "%WARN_LEVEL%"=="normal" (
    set WARN=-Wall -Wextra
) else if /i "%WARN_LEVEL%"=="strict" (
    set WARN=-Wall -Wextra -Wpedantic -Werror
) else (
    echo  [FAIL] Unknown WARN_LEVEL "%WARN_LEVEL%" -- use minimal, normal, or strict
    pause & exit /b 1
)

:: ---- Compile ----
echo  [....] Building %OUT% (%BUILD_TYPE%)
echo.

g++ -std=%STD% %OPT% %WARN% %EXTRA_FLAGS% ^
    -I"%INC%" -mwindows ^
    -o "%BUILD%\%OUT%" ^
    "%SRC%\main.cpp" ^
    "%SRC%\midi_parser.cpp" ^
    "%SRC%\psych_converter.cpp" ^
    "%SRC%\gui.cpp" ^
    "%SRC%\gui_logger.cpp" ^
    "%SRC%\progress_bar.cpp" ^
    -lcomctl32 -lcomdlg32 -lgdi32 -lshell32

if errorlevel 1 goto :build_failed

:: ---- Stage release ----
echo.
echo  [....] Staging release folder

copy /Y "%BUILD%\%OUT%" "%RELEASE%\" >nul 2>&1
echo         %OUT%

for %%D in (%BUNDLE_DLLS%) do (
    if exist "%MINGW_BIN%\%%D" (
        copy /Y "%MINGW_BIN%\%%D" "%RELEASE%\" >nul 2>&1
        echo         %%D
    ) else (
        echo         SKIP  %%D  (not in MinGW bin)
    )
)

if "%COPY_ASSETS%"=="1" (
    if exist "%ASSETS%" (
        if exist "%RELEASE%\assets" rmdir /s /q "%RELEASE%\assets" >nul 2>&1
        xcopy "%ASSETS%" "%RELEASE%\assets\" /E /I /H /Y >nul 2>&1
        echo         assets\
    )
)

for %%F in (README.md README.txt LICENSE LICENSE.txt CHANGELOG.md) do (
    if exist "%REPO_ROOT%\%%F" (
        copy /Y "%REPO_ROOT%\%%F" "%RELEASE%\" >nul 2>&1
        echo         %%F
    )
)

:: ---- Build info ----
echo App        = MIDI2Psych        > "%RELEASE%\build-info.txt"
echo Built      = %BUILD_TIME%     >> "%RELEASE%\build-info.txt"
echo Profile    = %BUILD_TYPE%     >> "%RELEASE%\build-info.txt"
echo Compiler   = %COMPILER_VER%   >> "%RELEASE%\build-info.txt"
echo Std        = %STD%            >> "%RELEASE%\build-info.txt"
echo Warnings   = %WARN_LEVEL%     >> "%RELEASE%\build-info.txt"
echo Binary     = %OUT%            >> "%RELEASE%\build-info.txt"

:: ---- Optional ZIP ----
if "%MAKE_ZIP%"=="1" (
    echo.
    echo  [....] Creating ZIP
    set ZIP_OUT=%ZIPS%\midi2psych_%BUILD_TYPE%.zip
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "Compress-Archive -Path '%RELEASE%\*' -DestinationPath '%ZIPS%\midi2psych_%BUILD_TYPE%.zip' -Force" >nul 2>&1
    if exist "%ZIPS%\midi2psych_%BUILD_TYPE%.zip" (
        echo         %ZIPS%\midi2psych_%BUILD_TYPE%.zip
    ) else (
        echo         WARN: ZIP did not complete
    )
)

:: ---- Done ----
for %%S in ("%RELEASE%\%OUT%") do set FILE_KB=%%~zS
set /a FILE_KB=%FILE_KB% / 1024

echo.
echo  =========================================
echo   BUILD SUCCESSFUL
echo  =========================================
echo.
echo   Binary  : %RELEASE%\%OUT%
echo   Size    : %FILE_KB% KB
echo   Folder  : %RELEASE%
echo.
pause
exit /b 0

:build_failed
echo.
echo  =========================================
echo   BUILD FAILED
echo  =========================================
echo.
echo   Check the compiler output above.
echo.
echo   Common fixes:
echo     - Remove -march=native from OPT_FLAGS if your CPU does not support it
echo     - Remove -flto if you get linker errors
echo     - Check that all headers exist in include\
echo.
pause
exit /b 1