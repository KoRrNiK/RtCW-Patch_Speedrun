@echo off
setlocal EnableExtensions

call "%~dp0build.bat" Debug x64
exit /b %ERRORLEVEL%
