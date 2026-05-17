@echo off
setlocal EnableExtensions

:: RtCW light patch x86 build helper.
:: Builds the Win32 solution, then stages the executable, game DLLs, and pk3
:: under code\src\bin\<Debug_x86|Release_x86>.

for %%d in ("%~dp0..") do set "ROOT=%%~fd"

set "SLN=%ROOT%\code\src\wolf.sln"
set "CONFIG=%~1"
set "PLATFORM=Win32"

if "%CONFIG%"=="" set "CONFIG=Debug"
if /i "%CONFIG%"=="debug" set "CONFIG=Debug"
if /i "%CONFIG%"=="release" set "CONFIG=Release"

if /i not "%CONFIG%"=="Debug" if /i not "%CONFIG%"=="Release" (
    echo Usage: tools\build.bat [Debug^|Release]
    exit /b 1
)

set "BIN_CONFIG=%CONFIG%_x86"
set "BIN_DIR=%ROOT%\code\src\bin\%BIN_CONFIG%"
set "MAIN_DIR=%BIN_DIR%\Main"

set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" 2^>nul`) do (
        set "MSBUILD=%%i"
    )
)

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
    echo ERROR: MSBuild not found. Install Visual Studio or Build Tools.
    exit /b 1
)

echo.
echo ================================================================
echo   RtCW light patch build: %CONFIG% x86
echo ================================================================
echo MSBuild: %MSBUILD%
echo Output : %BIN_DIR%
echo.

"%MSBUILD%" "%SLN%" /m /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /nologo /nr:false /v:minimal
if errorlevel 1 (
    echo.
    echo ERROR: build failed.
    exit /b 1
)

if not exist "%BIN_DIR%\WolfSP.exe" (
    echo ERROR: WolfSP.exe was not produced in %BIN_DIR%.
    exit /b 1
)
if not exist "%MAIN_DIR%" mkdir "%MAIN_DIR%"

if not exist "%BIN_DIR%\cgamex86.dll" (
    echo ERROR: cgamex86.dll was not produced in %BIN_DIR%.
    exit /b 1
)
if not exist "%BIN_DIR%\qagamex86.dll" (
    echo ERROR: qagamex86.dll was not produced in %BIN_DIR%.
    exit /b 1
)
if not exist "%BIN_DIR%\uix86.dll" (
    echo ERROR: uix86.dll was not produced in %BIN_DIR%.
    exit /b 1
)

set "SOURCE_MAIN=%ROOT%\code\src\bin\Debug_x64\Main"
if not exist "%MAIN_DIR%\pak0.pk3" if exist "%SOURCE_MAIN%\pak0.pk3" (
    echo Seeding local game data from bin\Debug_x64\Main...
    robocopy "%SOURCE_MAIN%" "%MAIN_DIR%" *.pk3 /XF sp_speedrun.pk3 /E >nul
    if errorlevel 8 exit /b %ERRORLEVEL%
)

call "%~dp0pack_pk3.bat" "%BIN_CONFIG%"
if errorlevel 1 exit /b 1

echo.
echo ================================================================
echo   BUILD COMPLETE
echo ================================================================
echo   EXE: %BIN_DIR%\WolfSP.exe
echo   PK3: %MAIN_DIR%\sp_speedrun.pk3
echo.

endlocal
