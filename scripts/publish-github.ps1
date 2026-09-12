$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (!(Get-Command git -ErrorAction SilentlyContinue)) { throw 'git is required' }
Set-Location $root
if (!(Test-Path .git)) { git init }
git add .
git commit -m 'Initial open-source IDM-C project' 2>$null
Write-Host 'Local repository prepared.'
Write-Host 'Create an empty public repository on GitHub, then run:'
Write-Host '  git remote add origin https://github.com/YOUR-USER/idm-c.git'
Write-Host '  git branch -M main'
Write-Host '  git push -u origin main'
