@echo off
REM Build RtCW_LiveSplit.exe standalone viewer
REM Requires Visual Studio Developer Command Prompt (cl.exe in PATH)

setlocal

REM Find cl.exe via vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Run from VS Developer Command Prompt instead.
    goto :try_direct
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo ERROR: Visual Studio not found.
    exit /b 1
)

REM Setup environment
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1

:try_direct
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found. Please run from a Visual Studio Developer Command Prompt.
    exit /b 1
)

echo Building RtCW_LiveSplit.exe ...
rc.exe livesplit.rc
if errorlevel 1 (
    echo RC COMPILE FAILED
    exit /b 1
)
cl.exe /O2 /W3 /I"../" /Fe:RtCW_LiveSplit.exe main.c livesplit.res /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib comctl32.lib uxtheme.lib shell32.lib
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo BUILD SUCCESSFUL: RtCW_LiveSplit.exe
