@echo off
setlocal EnableExtensions

call "%~dp0pack_release.bat" Release x64
exit /b %ERRORLEVEL%
