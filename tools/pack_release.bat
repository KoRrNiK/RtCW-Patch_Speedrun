@echo off
setlocal EnableExtensions

:: ================================================================
::  RtCW Speedrun Patch -- Release Packer
::
::  Collects built binaries and assets into a ready-to-distribute
::  .zip archive.
::
::  Contents:
::    WolfSP.exe              - game executable
::    RtCW_LiveSplit.exe       - LiveSplit standalone app
::    cgamex86.dll             - client game DLL
::    qagamex86.dll            - server game DLL
::    uix86.dll                - UI DLL
::    Main\sp_speedrun.pk3     - speedrun assets
::    Main\speedrun_defaults.cfg
::
::  Output:
::    RtCW_SpeedrunPatch_v1.45b_update_2026-04-06.zip
::
::  Usage:  pack_release.bat [Debug|Release]
::          (defaults to Release if no argument given)
:: ================================================================

for %%d in ("%~dp0..") do set "ROOT=%%~fd"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
if /i "%CONFIG%"=="debug" set "CONFIG=Debug"
if /i "%CONFIG%"=="release" set "CONFIG=Release"
if /i not "%CONFIG%"=="Debug" if /i not "%CONFIG%"=="Release" (
    echo Usage: tools\pack_release.bat [Debug^|Release]
    exit /b 1
)
set "BIN=%ROOT%\code\src\bin\%CONFIG%"
set "MAIN=%BIN%\Main"

:: ---- Version from q_shared.h ----
set "VER=unknown"
for /f "tokens=3 delims= " %%v in ('findstr /C:"#define PRODUCT_VERSION" "%ROOT%\code\src\game\q_shared.h"') do (
    set "VER=%%~v"
)

:: ---- Date stamp (YYYY-MM-DD) ----
for /f "tokens=2 delims==" %%d in ('wmic os get localdatetime /value 2^>nul') do set "DT=%%d"
set "DATESTR=%DT:~0,4%-%DT:~4,2%-%DT:~6,2%"

set "ZIP_SUFFIX="
if /i not "%CONFIG%"=="Release" set "ZIP_SUFFIX=_%CONFIG%"
set "ZIPNAME=RtCW_SpeedrunPatch_v%VER%_update_%DATESTR%%ZIP_SUFFIX%"
set "STAGING=%ROOT%\tools\_release_tmp\%ZIPNAME%"
set "OUTZIP=%ROOT%\tools\%ZIPNAME%.zip"

echo.
echo ================================================================
echo   RtCW Speedrun Patch -- Release Packer
echo   Config: %CONFIG%   Version: v%VER%   Date: %DATESTR%
echo ================================================================
echo.

:: ---- Verify required files exist ----
set "MISSING=0"

if not exist "%BIN%\WolfSP.exe" (
    echo [ERROR] WolfSP.exe not found in %BIN%
    set "MISSING=1"
)
if not exist "%BIN%\cgamex86.dll" (
    echo [ERROR] cgamex86.dll not found in %BIN%
    set "MISSING=1"
)
if not exist "%BIN%\qagamex86.dll" (
    echo [ERROR] qagamex86.dll not found in %BIN%
    set "MISSING=1"
)
if not exist "%BIN%\uix86.dll" (
    echo [ERROR] uix86.dll not found in %BIN%
    set "MISSING=1"
)
if not exist "%MAIN%\sp_speedrun.pk3" (
    echo [ERROR] sp_speedrun.pk3 not found in %MAIN%
    set "MISSING=1"
)

if "%MISSING%"=="1" (
    echo.
    echo Build first with: tools\build.bat %CONFIG%
    exit /b 1
)

:: ---- Clean staging area ----
if exist "%ROOT%\tools\_release_tmp" rmdir /s /q "%ROOT%\tools\_release_tmp"
mkdir "%STAGING%"
mkdir "%STAGING%\Main"

echo [1/4] Copying engine...
copy /y "%BIN%\WolfSP.exe" "%STAGING%\" >nul
if exist "%BIN%\RtCW_LiveSplit.exe" (
    copy /y "%BIN%\RtCW_LiveSplit.exe" "%STAGING%\" >nul
)
if exist "%BIN%\RtCW_RaceHost.exe" (
    copy /y "%BIN%\RtCW_RaceHost.exe" "%STAGING%\" >nul
)

echo [2/4] Copying DLLs...
copy /y "%BIN%\cgamex86.dll"  "%STAGING%\" >nul
copy /y "%BIN%\qagamex86.dll" "%STAGING%\" >nul
copy /y "%BIN%\uix86.dll"    "%STAGING%\" >nul

echo [3/4] Copying assets...
copy /y "%MAIN%\sp_speedrun.pk3"       "%STAGING%\Main\" >nul
copy /y "%MAIN%\speedrun_defaults.cfg" "%STAGING%\Main\" >nul 2>nul

echo [4/4] Creating ZIP...

:: Remove old zip if exists
if exist "%OUTZIP%" del /f "%OUTZIP%"

:: Use PowerShell to create ZIP (available on all modern Windows)
powershell -NoProfile -Command ^
    "Compress-Archive -Path '%STAGING%\*' -DestinationPath '%OUTZIP%' -Force"

if errorlevel 1 (
    echo [ERROR] Failed to create ZIP.
    exit /b 1
)

:: Clean up staging
rmdir /s /q "%ROOT%\tools\_release_tmp"

echo.
echo ================================================================
echo   DONE!  Created: tools\%ZIPNAME%.zip
echo ================================================================
echo.

:: Show file size
for %%f in ("%OUTZIP%") do (
    set "FSIZE=%%~zf"
)
call set "FSIZE=%%FSIZE%%"
echo   Size: %FSIZE% bytes
echo.

endlocal
