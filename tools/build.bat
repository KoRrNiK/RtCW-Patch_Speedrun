@echo off
setlocal EnableExtensions

:: ================================================================
::  RtCW Patch -- Full Build Script
::  Compiles the engine and packs the PK3.
::
::  Output layout:
::    bin\<Config>\       - WolfSP.exe + DLLs (engine binaries)
::    bin\<Config>\Main\  - sp_speedrun.pk3   (game assets)
::
::  Usage:  build.bat [Debug|Release] [Win32|x64]
::          (defaults to Debug if no argument given)
:: ================================================================

for %%d in ("%~dp0..") do set "ROOT=%%~fd"
set "SLN=%ROOT%\code\src\wolf.sln"
set "CONFIG=%~1"
set "PLATFORM=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"
if /i "%CONFIG%"=="debug" set "CONFIG=Debug"
if /i "%CONFIG%"=="release" set "CONFIG=Release"
if /i not "%CONFIG%"=="Debug" if /i not "%CONFIG%"=="Release" (
    echo Usage: tools\build.bat [Debug^|Release] [Win32^|x64]
    exit /b 1
)
if "%PLATFORM%"=="" set "PLATFORM=Win32"
if /i "%PLATFORM%"=="win32" set "PLATFORM=Win32"
if /i "%PLATFORM%"=="x86" set "PLATFORM=Win32"
if /i "%PLATFORM%"=="x64" set "PLATFORM=x64"
if /i not "%PLATFORM%"=="Win32" if /i not "%PLATFORM%"=="x64" (
    echo Usage: tools\build.bat [Debug^|Release] [Win32^|x64]
    exit /b 1
)
set "BIN_CONFIG=%CONFIG%"
if /i "%PLATFORM%"=="x64" set "BIN_CONFIG=%CONFIG%_x64"
set "BIN_DIR=%ROOT%\code\src\bin\%BIN_CONFIG%"
set "MAIN_DIR=%BIN_DIR%\Main"
set "PK3_SCRIPT=%~dp0pack_pk3.bat"

:: Colours via PowerShell helper
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -Command"

%PS% "Write-Host ''; Write-Host '================================================================' -ForegroundColor Cyan; Write-Host '  RtCW Patch -- Full Build  (%CONFIG%|%PLATFORM%)' -ForegroundColor White; Write-Host '================================================================' -ForegroundColor Cyan"

:: ----------------------------------------------------------------
::  1. Locate MSBuild via vswhere
:: ----------------------------------------------------------------
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" 2^>nul`) do (
        set "MSBUILD=%%i"
    )
)

:: Fallback: try common paths
if "%MSBUILD%"=="" (
    for %%p in (
        "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    ) do (
        if exist %%p (
            set "MSBUILD=%%~p"
            goto :found_msbuild
        )
    )
)
:found_msbuild

if "%MSBUILD%"=="" (
    %PS% "Write-Host 'ERROR: MSBuild not found! Install Visual Studio or Build Tools.' -ForegroundColor Red"
    pause
    exit /b 1
)

%PS% "Write-Host ('MSBuild : ' + '%MSBUILD%') -ForegroundColor DarkGray"
%PS% "Write-Host ('Solution: ' + '%SLN%') -ForegroundColor DarkGray"
%PS% "Write-Host ('bin     : ' + '%BIN_DIR%') -ForegroundColor DarkGray"
%PS% "Write-Host ('Main    : ' + '%MAIN_DIR%') -ForegroundColor DarkGray"
%PS% "Write-Host ''"

:: ----------------------------------------------------------------
::  2. Build the solution  (EXE + DLLs > bin\)
:: ----------------------------------------------------------------
%PS% "Write-Host '================================' -ForegroundColor DarkCyan; Write-Host '  STEP 1: Compile Engine + DLLs  (bin\)' -ForegroundColor Cyan; Write-Host '================================' -ForegroundColor DarkCyan"

"%MSBUILD%" "%SLN%" /t:extractfuncs;wolf;cgame;game;ui /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /nologo /nr:false /v:minimal

if %ERRORLEVEL% neq 0 (
    echo.
    %PS% "Write-Host 'BUILD FAILED!' -ForegroundColor Red"
    pause
    exit /b 1
)

if not exist "%BIN_DIR%\WolfSP.exe" (
    %PS% "Write-Host ('ERROR: WolfSP.exe not found in %BIN_DIR%') -ForegroundColor Red"
    pause
    exit /b 1
)

:: Keep Main asset-only. Game DLLs are built next to WolfSP.exe.
if not exist "%MAIN_DIR%" mkdir "%MAIN_DIR%"

%PS% "Write-Host ''; Write-Host 'Compile OK -- EXE/DLLs in bin\%BIN_CONFIG%\' -ForegroundColor Green; Write-Host ''"

:: ----------------------------------------------------------------
::  3. Build LiveSplit standalone  (> bin\)
:: ----------------------------------------------------------------
%PS% "Write-Host '================================' -ForegroundColor DarkCyan; Write-Host '  STEP 2: Build RtCW_LiveSplit.exe' -ForegroundColor Cyan; Write-Host '================================' -ForegroundColor DarkCyan"

"%MSBUILD%" "%ROOT%\code\src\livesplit_app\livesplit_app.vcxproj" /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% "/p:SolutionDir=%ROOT%\code\src\\" /nologo /nr:false /v:minimal

if %ERRORLEVEL% neq 0 (
    echo.
    %PS% "Write-Host 'LiveSplit BUILD FAILED!' -ForegroundColor Red"
    pause
    exit /b 1
)

%PS% "Write-Host ''; Write-Host 'LiveSplit OK -- RtCW_LiveSplit.exe in bin\%BIN_CONFIG%\' -ForegroundColor Green; Write-Host ''"

:: ----------------------------------------------------------------
::  4. Build RaceHost standalone  (> bin\)
:: ----------------------------------------------------------------
%PS% "Write-Host '================================' -ForegroundColor DarkCyan; Write-Host '  STEP 3: Build RtCW_RaceHost.exe' -ForegroundColor Cyan; Write-Host '================================' -ForegroundColor DarkCyan"

"%MSBUILD%" "%ROOT%\code\src\race_host_app\race_host_app.vcxproj" /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% "/p:SolutionDir=%ROOT%\code\src\\" /nologo /nr:false /v:minimal

if %ERRORLEVEL% neq 0 (
    echo.
    %PS% "Write-Host 'RaceHost BUILD FAILED!' -ForegroundColor Red"
    pause
    exit /b 1
)

%PS% "Write-Host ''; Write-Host 'RaceHost OK -- RtCW_RaceHost.exe in bin\%BIN_CONFIG%\' -ForegroundColor Green; Write-Host ''"

:: ----------------------------------------------------------------
::  5. Pack PK3  (> Main\)
:: ----------------------------------------------------------------
%PS% "Write-Host '================================' -ForegroundColor DarkCyan; Write-Host '  STEP 4: Pack PK3  (Main\)' -ForegroundColor Cyan; Write-Host '================================' -ForegroundColor DarkCyan"

if not exist "%MAIN_DIR%" mkdir "%MAIN_DIR%"

if exist "%PK3_SCRIPT%" (
    call "%PK3_SCRIPT%" "%BIN_CONFIG%"
    if %ERRORLEVEL% neq 0 (
        echo.
        %PS% "Write-Host 'PK3 PACK FAILED!' -ForegroundColor Red"
        pause
        exit /b 1
    )
) else (
    %PS% "Write-Host 'WARNING: pack_pk3.bat not found, skipping PK3 step.' -ForegroundColor Yellow"
)

:: ----------------------------------------------------------------
::  Done
:: ----------------------------------------------------------------
echo.
%PS% "Write-Host '================================================================' -ForegroundColor Green; Write-Host '  BUILD COMPLETE' -ForegroundColor White; Write-Host '================================================================' -ForegroundColor Green"
%PS% "Write-Host ('  EXE : %BIN_DIR%\WolfSP.exe') -ForegroundColor Gray"
%PS% "Write-Host ('  LS  : %BIN_DIR%\RtCW_LiveSplit.exe') -ForegroundColor Gray"
%PS% "Write-Host ('  RH  : %BIN_DIR%\RtCW_RaceHost.exe') -ForegroundColor Gray"
%PS% "Write-Host ('  PK3 : %MAIN_DIR%\sp_speedrun.pk3') -ForegroundColor Gray"
%PS% "Write-Host ''"

pause
