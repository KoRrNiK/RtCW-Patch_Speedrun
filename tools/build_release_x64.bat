@echo off
setlocal EnableExtensions

call "%~dp0build.bat" Release x64
exit /b %ERRORLEVEL%
