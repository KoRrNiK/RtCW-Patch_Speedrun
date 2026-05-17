@echo off
setlocal EnableExtensions

for %%d in ("%~dp0..") do set "ROOT=%%~fd"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug_x86"
if /i "%CONFIG%"=="debug" set "CONFIG=Debug_x86"
if /i "%CONFIG%"=="release" set "CONFIG=Release_x86"
if /i "%CONFIG%"=="debug_x86" set "CONFIG=Debug_x86"
if /i "%CONFIG%"=="release_x86" set "CONFIG=Release_x86"

if /i not "%CONFIG%"=="Debug_x86" if /i not "%CONFIG%"=="Release_x86" (
    echo Usage: tools\pack_pk3.bat [Debug^|Release^|Debug_x86^|Release_x86]
    exit /b 1
)

set "SRC=%ROOT%\assets\_speedrun_pk3"
set "OUT=%ROOT%\code\src\bin\%CONFIG%\Main"
set "PK3=%OUT%\sp_speedrun.pk3"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "$src = [System.IO.Path]::GetFullPath('%SRC%');" ^
    "$out = [System.IO.Path]::GetFullPath('%OUT%');" ^
    "$pk3 = [System.IO.Path]::GetFullPath('%PK3%');" ^
    "if (-not (Test-Path -LiteralPath $src)) { throw 'Missing assets/_speedrun_pk3 folder.' }" ^
    "if (-not (Test-Path -LiteralPath $out)) { New-Item -ItemType Directory -Path $out | Out-Null }" ^
    "$tmp = $pk3 + '.tmp.zip';" ^
    "if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }" ^
    "if (Test-Path -LiteralPath $pk3) { Remove-Item -LiteralPath $pk3 -Force }" ^
    "Add-Type -AssemblyName 'System.IO.Compression.FileSystem';" ^
    "[System.IO.Compression.ZipFile]::CreateFromDirectory($src, $tmp, [System.IO.Compression.CompressionLevel]::Optimal, $false);" ^
    "Move-Item -LiteralPath $tmp -Destination $pk3 -Force;" ^
    "$size = (Get-Item -LiteralPath $pk3).Length;" ^
    "Write-Host ('Packed ' + $pk3 + ' (' + $size + ' bytes)') -ForegroundColor Green;"

if errorlevel 1 (
    echo ERROR: failed to create sp_speedrun.pk3.
    exit /b 1
)

endlocal
