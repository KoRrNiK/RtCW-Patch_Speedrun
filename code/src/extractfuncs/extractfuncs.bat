@echo off
setlocal EnableExtensions

set "CONFIG=%~1"
set "PLATFORM=%~2"
set "TOOL=%~dp0extractfuncs.exe"

if /i "%PLATFORM%"=="x64" (
    if /i "%CONFIG%"=="Debug" set "TOOL=%~dp0Debug_x64\extractfuncs_x64.exe"
    if /i "%CONFIG%"=="Release" set "TOOL=%~dp0Release_x64\extractfuncs_x64.exe"
)

if not exist "%TOOL%" (
    echo extractfuncs tool not found: "%TOOL%"
    exit /b 1
)

pushd "%~dp0..\game" || exit /b 1
"%TOOL%" *.c
set result=%ERRORLEVEL%
popd
exit /b %result%
