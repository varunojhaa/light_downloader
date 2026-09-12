$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
& (Join-Path $root 'build-llvm.ps1')
$iscc = @(
    (Get-Command iscc -ErrorAction SilentlyContinue).Source
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
    'C:\Program Files\Inno Setup 6\ISCC.exe'
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (!$iscc) { throw 'Inno Setup is required: install Inno Setup 6 or add ISCC.exe to PATH.' }
& $iscc (Join-Path $root 'packaging\windows\light-downloader.iss')
if ($LASTEXITCODE) { throw 'Inno Setup failed.' }
Write-Host "Installer created in $(Join-Path $root 'dist')"