@echo off
setlocal EnableExtensions

for %%d in ("%~dp0..") do set "ROOT=%%~fd"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release_x86"
if /i "%CONFIG%"=="debug" set "CONFIG=Debug_x86"
if /i "%CONFIG%"=="release" set "CONFIG=Release_x86"
if /i "%CONFIG%"=="debug_x86" set "CONFIG=Debug_x86"
if /i "%CONFIG%"=="release_x86" set "CONFIG=Release_x86"

if /i not "%CONFIG%"=="Debug_x86" if /i not "%CONFIG%"=="Release_x86" (
    echo Usage: tools\pack_release.bat [Debug^|Release^|Debug_x86^|Release_x86]
    exit /b 1
)

set "BIN=%ROOT%\code\src\bin\%CONFIG%"
set "MAIN=%BIN%\Main"

for /f %%d in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd"') do set "DATESTR=%%d"
set "ZIPNAME=RtCW_LightPatch_%CONFIG%_%DATESTR%"
set "STAGING=%ROOT%\tools\_release_tmp\%ZIPNAME%"
set "OUTZIP=%ROOT%\tools\%ZIPNAME%.zip"

set "MISSING=0"
if not exist "%BIN%\WolfSP.exe" (
    set "MISSING=1"
    echo ERROR: missing %BIN%\WolfSP.exe
)
if not exist "%BIN%\cgamex86.dll" (
    set "MISSING=1"
    echo ERROR: missing %BIN%\cgamex86.dll
)
if not exist "%BIN%\qagamex86.dll" (
    set "MISSING=1"
    echo ERROR: missing %BIN%\qagamex86.dll
)
if not exist "%BIN%\uix86.dll" (
    set "MISSING=1"
    echo ERROR: missing %BIN%\uix86.dll
)
if not exist "%MAIN%\sp_speedrun.pk3" (
    set "MISSING=1"
    echo ERROR: missing %MAIN%\sp_speedrun.pk3
)

if "%MISSING%"=="1" (
    echo.
    echo Build first with: tools\build.bat %CONFIG:_x86=%
    exit /b 1
)

if exist "%ROOT%\tools\_release_tmp" rmdir /s /q "%ROOT%\tools\_release_tmp"
mkdir "%STAGING%"
mkdir "%STAGING%\Main"

copy /y "%BIN%\WolfSP.exe" "%STAGING%\" >nul
copy /y "%BIN%\cgamex86.dll" "%STAGING%\" >nul
copy /y "%BIN%\qagamex86.dll" "%STAGING%\" >nul
copy /y "%BIN%\uix86.dll" "%STAGING%\" >nul
copy /y "%MAIN%\sp_speedrun.pk3" "%STAGING%\Main\" >nul

if exist "%OUTZIP%" del /f "%OUTZIP%"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "Compress-Archive -Path '%STAGING%\*' -DestinationPath '%OUTZIP%' -Force"

if errorlevel 1 (
    echo ERROR: failed to create release zip.
    exit /b 1
)

rmdir /s /q "%ROOT%\tools\_release_tmp"

echo.
echo Created: tools\%ZIPNAME%.zip
echo.

endlocal
