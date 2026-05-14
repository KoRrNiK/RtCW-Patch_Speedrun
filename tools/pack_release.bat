@echo off
setlocal EnableExtensions

:: ================================================================
::  RtCW Speedrun Patch -- Release Packer
::
::  Collects built binaries and assets into a ready-to-distribute
::  .zip archive.
::
::  Usage:  pack_release.bat [Debug|Release] [Win32|x64]
::          defaults to Release x64
:: ================================================================

for %%d in ("%~dp0..") do set "ROOT=%%~fd"
set "CONFIG=%~1"
set "PLATFORM=%~2"

if "%CONFIG%"=="" set "CONFIG=Release"
if /i "%CONFIG%"=="debug" set "CONFIG=Debug"
if /i "%CONFIG%"=="release" set "CONFIG=Release"
if /i not "%CONFIG%"=="Debug" if /i not "%CONFIG%"=="Release" (
    echo Usage: tools\pack_release.bat [Debug^|Release] [Win32^|x64]
    exit /b 1
)

if "%PLATFORM%"=="" set "PLATFORM=x64"
if /i "%PLATFORM%"=="win32" set "PLATFORM=Win32"
if /i "%PLATFORM%"=="x86" set "PLATFORM=Win32"
if /i "%PLATFORM%"=="x64" set "PLATFORM=x64"
if /i not "%PLATFORM%"=="Win32" if /i not "%PLATFORM%"=="x64" (
    echo Usage: tools\pack_release.bat [Debug^|Release] [Win32^|x64]
    exit /b 1
)

set "BIN_CONFIG=%CONFIG%"
set "ARCH_TAG=Win32"
set "DLL_ARCH=x86"
if /i "%PLATFORM%"=="x64" (
    set "BIN_CONFIG=%CONFIG%_x64"
    set "ARCH_TAG=x64"
    set "DLL_ARCH=x64"
)

set "BIN=%ROOT%\code\src\bin\%BIN_CONFIG%"
set "MAIN=%BIN%\Main"
set "GAME_DLLS=cgame%DLL_ARCH%.dll qagame%DLL_ARCH%.dll ui%DLL_ARCH%.dll"

:: ---- Version from q_shared.h ----
set "VER=unknown"
for /f "tokens=3 delims= " %%v in ('findstr /C:"#define PRODUCT_VERSION" "%ROOT%\code\src\game\q_shared.h"') do (
    set "VER=%%~v"
)

:: ---- Date stamp (YYYY-MM-DD) ----
for /f "tokens=2 delims==" %%d in ('wmic os get localdatetime /value 2^>nul') do set "DT=%%d"
if "%DT%"=="" (
    for /f %%d in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd"') do set "DATESTR=%%d"
) else (
    set "DATESTR=%DT:~0,4%-%DT:~4,2%-%DT:~6,2%"
)

set "ZIP_SUFFIX=_%ARCH_TAG%"
if /i not "%CONFIG%"=="Release" set "ZIP_SUFFIX=_%CONFIG%_%ARCH_TAG%"
set "ZIPNAME=RtCW_SpeedrunPatch_v%VER%_update_%DATESTR%%ZIP_SUFFIX%"
set "STAGING=%ROOT%\tools\_release_tmp\%ZIPNAME%"
set "OUTZIP=%ROOT%\tools\%ZIPNAME%.zip"

echo.
echo ================================================================
echo   RtCW Speedrun Patch -- Release Packer
echo   Config: %CONFIG%   Platform: %PLATFORM%   Version: v%VER%
echo   Output: tools\%ZIPNAME%.zip
echo ================================================================
echo.

:: ---- Verify required files exist ----
set "MISSING=0"

if not exist "%BIN%\WolfSP.exe" (
    echo [ERROR] WolfSP.exe not found in %BIN%
    set "MISSING=1"
)
if not exist "%BIN%\SDL3.dll" (
    echo [ERROR] SDL3.dll not found in %BIN%
    set "MISSING=1"
)
for %%f in (%GAME_DLLS%) do (
    if not exist "%BIN%\%%f" (
        echo [ERROR] %%f not found in %BIN%
        set "MISSING=1"
    )
)
if not exist "%MAIN%\sp_speedrun.pk3" (
    echo [ERROR] sp_speedrun.pk3 not found in %MAIN%
    set "MISSING=1"
)

if "%MISSING%"=="1" (
    echo.
    echo Build first with: tools\build.bat %CONFIG% %PLATFORM%
    exit /b 1
)

:: ---- Clean staging area ----
if exist "%ROOT%\tools\_release_tmp" rmdir /s /q "%ROOT%\tools\_release_tmp"
mkdir "%STAGING%"
mkdir "%STAGING%\Main"

echo [1/5] Copying engine and runtime...
copy /y "%BIN%\WolfSP.exe" "%STAGING%\" >nul
copy /y "%BIN%\SDL3.dll" "%STAGING%\" >nul

if exist "%BIN%\RtCW_LiveSplit.exe" (
    copy /y "%BIN%\RtCW_LiveSplit.exe" "%STAGING%\" >nul
)
if exist "%BIN%\RtCW_RaceHost.exe" (
    copy /y "%BIN%\RtCW_RaceHost.exe" "%STAGING%\" >nul
)
if exist "%BIN%\serverconfig.cfg" (
    copy /y "%BIN%\serverconfig.cfg" "%STAGING%\" >nul
)

echo [2/5] Copying native game DLLs next to WolfSP.exe...
for %%f in (%GAME_DLLS%) do (
    copy /y "%BIN%\%%f" "%STAGING%\%%f" >nul
)

echo [3/5] Copying speedrun assets...
copy /y "%MAIN%\sp_speedrun.pk3" "%STAGING%\Main\" >nul
if exist "%MAIN%\speedrun_defaults.cfg" (
    copy /y "%MAIN%\speedrun_defaults.cfg" "%STAGING%\Main\" >nul
) else if exist "%ROOT%\assets\_speedrun_pk3\speedrun_defaults.cfg" (
    copy /y "%ROOT%\assets\_speedrun_pk3\speedrun_defaults.cfg" "%STAGING%\Main\" >nul
)

echo [4/5] Creating ZIP...
if exist "%OUTZIP%" del /f "%OUTZIP%"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "Compress-Archive -Path '%STAGING%\*' -DestinationPath '%OUTZIP%' -Force"

if errorlevel 1 (
    echo [ERROR] Failed to create ZIP.
    exit /b 1
)

echo [5/5] Cleaning staging...
rmdir /s /q "%ROOT%\tools\_release_tmp"

echo.
echo ================================================================
echo   DONE! Created: tools\%ZIPNAME%.zip
echo ================================================================
echo.

for %%f in ("%OUTZIP%") do (
    echo   Size: %%~zf bytes
)
echo.

endlocal
