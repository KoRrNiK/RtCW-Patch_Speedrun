@echo off
setlocal

set "SRC=%~dp0..\assets\_speedrun_pk3"
set "ROOT=%~dp0.."
set "OUT=%ROOT%\code\src\bin\Debug\Main"
set "PK3=%OUT%\sp_speedrun.pk3"

echo ================================
echo Packing pk3 file from folder: %SRC%
echo Output: %PK3%
echo ================================
echo.

if not exist "%SRC%" (
    echo Error: source folder assets\_speedrun_pk3 not found!
    pause
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

if exist "%PK3%" del /q "%PK3%"

powershell -NoProfile -Command ^
    "$src = '%SRC%';" ^
    "$pk3 = '%PK3%';" ^
    "$tmp = $pk3 + '.tmp.zip';" ^
    "if (Test-Path $tmp) { Remove-Item $tmp -Force };" ^
    "Add-Type -Assembly 'System.IO.Compression.FileSystem';" ^
    "$zip = [System.IO.Compression.ZipFile]::Open($tmp, 'Create');" ^
    "$files = Get-ChildItem -Path $src -Recurse -File | Where-Object { $_.Name -ne 'sp_speedrun.pk3' };" ^
    "foreach ($f in $files) {" ^
    "  $rel = $f.FullName.Substring($src.Length + 1).Replace('\', '/');" ^
    "  [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $rel, 'Optimal');" ^
    "  Write-Host ('  + ' + $rel);" ^
    "}" ^
    "$zip.Dispose();" ^
    "Move-Item -Path $tmp -Destination $pk3 -Force;" ^
    "Write-Host '';" ^
    "Write-Host ('Rozmiar: ' + [math]::Round((Get-Item $pk3).Length / 1KB, 1).ToString() + ' KB');"

if %ERRORLEVEL% neq 0 (
    echo.
    echo Error: no created pk3 file!s
    pause
    exit /b 1
)

echo.
echo ================================
echo  Finish! ^> %PK3%
echo ================================
echo.
pause
