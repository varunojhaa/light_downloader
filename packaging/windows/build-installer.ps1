$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
& (Join-Path $root 'build-llvm.ps1')
if (!(Get-Command iscc -ErrorAction SilentlyContinue)) { throw 'Inno Setup is required: install ISCC.exe and add it to PATH.' }
& iscc (Join-Path $root 'packaging\windows\light-downloader.iss')
if ($LASTEXITCODE) { throw 'Inno Setup failed.' }
Write-Host "Installer created in $(Join-Path $root 'dist')"