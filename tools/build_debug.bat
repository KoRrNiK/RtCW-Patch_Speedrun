@echo off
setlocal EnableExtensions

call "%~dp0build.bat" Debug
exit /b %ERRORLEVEL%