@echo off
setlocal EnableExtensions

for %%d in ("%~dp0..") do set "ROOT=%%~fd"
set "PROJECT=%ROOT%\code\src\wolf.vcxproj"
set "SOLUTION_DIR=%ROOT%\code\src\\"
set "CONFIG=Debug"
set "PLATFORM=x64"

echo.
echo ================================================================
echo   Renderer2 quick build  (%CONFIG%^|%PLATFORM%)
echo ================================================================

tasklist /FI "IMAGENAME eq WolfSP.exe" 2>nul | find /I "WolfSP.exe" >nul
if errorlevel 1 goto :wolfsp_closed

echo Closing running WolfSP.exe so the linker can update it...
taskkill /F /T /IM WolfSP.exe >nul 2>nul
for /l %%i in (1,1,20) do (
    tasklist /FI "IMAGENAME eq WolfSP.exe" 2>nul | find /I "WolfSP.exe" >nul
    if errorlevel 1 goto :wolfsp_closed
    timeout /T 1 /NOBREAK >nul
)
echo WARNING: WolfSP.exe may still be running; linker can fail if a debugger owns it.
:wolfsp_closed

set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\amd64\MSBuild.exe" 2^>nul`) do (
        set "MSBUILD=%%i"
    )
)

if "%MSBUILD%"=="" (
    for %%p in (
        "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
    ) do (
        if exist %%p (
            set "MSBUILD=%%~p"
            goto :found_msbuild
        )
    )
)
:found_msbuild

if "%MSBUILD%"=="" (
    echo ERROR: MSBuild not found.
    exit /b 1
)

echo MSBuild: %MSBUILD%
echo Project: %PROJECT%
echo.

"%MSBUILD%" "%PROJECT%" /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% "/p:SolutionDir=%SOLUTION_DIR%" /nologo /nr:false /v:minimal
set "BUILD_RESULT=%ERRORLEVEL%"

if not "%BUILD_RESULT%"=="0" (
    echo.
    echo Renderer2 quick build failed.
    exit /b %BUILD_RESULT%
)

echo.
echo Renderer2 quick build complete.
exit /b 0
