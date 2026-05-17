@echo off
setlocal EnableExtensions

call "%~dp0pack_release.bat" Release
exit /b %ERRORLEVEL%
