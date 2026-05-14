@echo off
setlocal EnableExtensions

call "%~dp0build.bat" Release
exit /b %ERRORLEVEL%