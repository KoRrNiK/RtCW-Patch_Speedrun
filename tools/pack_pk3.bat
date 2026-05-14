@echo off
setlocal EnableExtensions

set "SRC=%~dp0..\assets\_speedrun_pk3"
set "ROOT=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
if /i "%CONFIG%"=="debug" set "CONFIG=Debug"
if /i "%CONFIG%"=="release" set "CONFIG=Release"
if /i not "%CONFIG%"=="Debug" if /i not "%CONFIG%"=="Release" (
    echo Usage: tools\pack_pk3.bat [Debug^|Release]
    pause
    exit /b 1
)
set "OUT=%ROOT%\code\src\bin\%CONFIG%\Main"
set "PK3=%OUT%\sp_speedrun.pk3"
set "STATE=%OUT%\sp_speedrun.state"
set "META=%OUT%\sp_speedrun.meta"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "" ^
    "function Write-Section($text) { Write-Host ''; Write-Host '================================' -ForegroundColor DarkCyan; Write-Host (' ' + $text) -ForegroundColor Cyan; Write-Host '================================' -ForegroundColor DarkCyan; }" ^
    "function Format-Size($bytes) {" ^
    "    if ($bytes -ge 1GB) { return ('{0:N2} GB' -f ($bytes / 1GB)) }" ^
    "    elseif ($bytes -ge 1MB) { return ('{0:N2} MB' -f ($bytes / 1MB)) }" ^
    "    elseif ($bytes -ge 1KB) { return ('{0:N1} KB' -f ($bytes / 1KB)) }" ^
    "    else { return ($bytes.ToString() + ' B') }" ^
    "}" ^
    "function Format-Diff($diff) {" ^
    "    if ($diff -gt 0) { return ('+' + (Format-Size $diff)) }" ^
    "    elseif ($diff -lt 0) { return ('-' + (Format-Size ([math]::Abs($diff)))) }" ^
    "    else { return '0 B' }" ^
    "}" ^
    "function Get-SourceSignature($src) {" ^
    "    $files = Get-ChildItem -Path $src -Recurse -File | Where-Object { $_.Name -ne 'sp_speedrun.pk3' } | Sort-Object FullName;" ^
    "    return ($files | ForEach-Object {" ^
    "        $rel = $_.FullName.Substring($src.Length + 1).Replace('\', '/');" ^
    "        '{0}|{1}|{2}' -f $rel, $_.Length, $_.LastWriteTimeUtc.Ticks" ^
    "    }) -join [Environment]::NewLine" ^
    "}" ^
    "" ^
    "$src = [System.IO.Path]::GetFullPath('%SRC%');" ^
    "$out = [System.IO.Path]::GetFullPath('%OUT%');" ^
    "$pk3 = [System.IO.Path]::GetFullPath('%PK3%');" ^
    "$state = [System.IO.Path]::GetFullPath('%STATE%');" ^
    "$meta = [System.IO.Path]::GetFullPath('%META%');" ^
    "" ^
    "Write-Section 'PK3 PACKER';" ^
    "Write-Host ('Configuration : ' + '%CONFIG%') -ForegroundColor Gray;" ^
    "Write-Host ('Source folder : ' + $src) -ForegroundColor Gray;" ^
    "Write-Host ('Output file   : ' + $pk3) -ForegroundColor Gray;" ^
    "" ^
    "if (-not (Test-Path $src)) {" ^
    "    Write-Host 'Error: source folder assets\\_speedrun_pk3 not found!' -ForegroundColor Red;" ^
    "    exit 1" ^
    "}" ^
    "" ^
    "if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }" ^
    "" ^
    "$newSignature = Get-SourceSignature $src;" ^
    "$oldSignature = if (Test-Path $state) { Get-Content $state -Raw } else { $null };" ^
    "$hadPreviousPk3 = Test-Path $pk3;" ^
    "$oldPk3Size = if ($hadPreviousPk3) { (Get-Item $pk3).Length } else { 0 };" ^
    "" ^
    "if ($oldSignature -and $oldSignature -eq $newSignature -and $hadPreviousPk3) {" ^
    "    Write-Host '';" ^
    "    Write-Host 'No changes detected since last build.' -ForegroundColor Yellow;" ^
    "    Write-Host ('Current pk3 size: ' + (Format-Size $oldPk3Size)) -ForegroundColor DarkYellow;" ^
    "    exit 0" ^
    "}" ^
    "" ^
    "if ($oldSignature) {" ^
    "    Write-Host 'Changes detected since last build.' -ForegroundColor Green;" ^
    "} else {" ^
    "    Write-Host 'First build or no previous state found.' -ForegroundColor Magenta;" ^
    "}" ^
    "" ^
    "if (Test-Path $pk3) { Remove-Item $pk3 -Force }" ^
    "$tmp = $pk3 + '.tmp.zip';" ^
    "if (Test-Path $tmp) { Remove-Item $tmp -Force }" ^
    "" ^
    "Add-Type -AssemblyName 'System.IO.Compression.FileSystem';" ^
    "$zip = [System.IO.Compression.ZipFile]::Open($tmp, 'Create');" ^
    "" ^
    "try {" ^
    "    $files = Get-ChildItem -Path $src -Recurse -File | Where-Object { $_.Name -ne 'sp_speedrun.pk3' } | Sort-Object FullName;" ^
    "    foreach ($f in $files) {" ^
    "        $rel = $f.FullName.Substring($src.Length + 1).Replace('\', '/');" ^
    "        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $rel, 'Optimal');" ^
    "        Write-Host ('  + ' + $rel) -ForegroundColor DarkGray;" ^
    "    }" ^
    "} finally {" ^
    "    $zip.Dispose();" ^
    "}" ^
    "" ^
    "Move-Item -Path $tmp -Destination $pk3 -Force;" ^
    "Set-Content -Path $state -Value $newSignature -Encoding UTF8;" ^
    "" ^
    "$newPk3Size = (Get-Item $pk3).Length;" ^
    "$diff = $newPk3Size - $oldPk3Size;" ^
    "" ^
    "$metaData = @(" ^
    "    'LastBuild=' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')," ^
    "    'Size=' + $newPk3Size" ^
    ");" ^
    "Set-Content -Path $meta -Value $metaData -Encoding UTF8;" ^
    "" ^
    "Write-Host '';" ^
    "Write-Section 'RESULT';" ^
    "Write-Host ('New size        : ' + (Format-Size $newPk3Size)) -ForegroundColor Cyan;" ^
    "if ($oldPk3Size -gt 0) {" ^
    "    Write-Host ('Previous size   : ' + (Format-Size $oldPk3Size)) -ForegroundColor Gray;" ^
    "    if ($diff -gt 0) {" ^
    "        Write-Host ('Change          : increased by ' + (Format-Size $diff)) -ForegroundColor Yellow;" ^
    "    } elseif ($diff -lt 0) {" ^
    "        Write-Host ('Change          : decreased by ' + (Format-Size ([math]::Abs($diff)))) -ForegroundColor Green;" ^
    "    } else {" ^
    "        Write-Host 'Change          : no size difference' -ForegroundColor DarkYellow;" ^
    "    }" ^
    "} else {" ^
    "    Write-Host 'Previous size   : none (new file)' -ForegroundColor Gray;" ^
    "}" ^
    "" ^
    "Write-Host ('Done: ' + $pk3) -ForegroundColor Green;" ^
    "exit 0"

if %ERRORLEVEL% neq 0 (
    echo.
    echo Error: failed to create pk3 file!
    pause
    exit /b 1
)

echo.
pause