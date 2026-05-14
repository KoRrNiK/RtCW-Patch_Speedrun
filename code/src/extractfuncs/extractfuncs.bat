@echo off
pushd "%~dp0..\game" || exit /b 1
"%~dp0extractfuncs.exe" *.c
set result=%ERRORLEVEL%
popd
exit /b %result%
