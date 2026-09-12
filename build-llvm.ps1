$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$llvm = 'C:\Program Files\LLVM\bin'
$clang = Join-Path $llvm 'clang-cl.exe'
$out = Join-Path $root 'build-llvm'
if (!(Test-Path $clang)) { throw "LLVM clang-cl.exe was not found at $clang" }
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
if (!(Test-Path (Join-Path $sdkRoot 'Include'))) { throw "Windows SDK headers were not found. Install the Windows 10/11 SDK, then rerun this script from a Developer PowerShell." }
$sdkVersion = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
$sdkInclude = Join-Path $sdkRoot "Include\$sdkVersion"
$sdkLib = Join-Path $sdkRoot "Lib\$sdkVersion"
$sdkFlags = @('/imsvc', (Join-Path $sdkInclude 'ucrt'), '/imsvc', (Join-Path $sdkInclude 'shared'), '/imsvc', (Join-Path $sdkInclude 'um'), '/imsvc', (Join-Path $sdkInclude 'winrt'), '/imsvc', (Join-Path $sdkInclude 'cppwinrt'), '/libpath:' + (Join-Path $sdkLib 'ucrt\x64'), '/libpath:' + (Join-Path $sdkLib 'um\x64'))
New-Item -ItemType Directory -Force $out | Out-Null

& $clang /nologo /O2 /W4 /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE @sdkFlags `
  (Join-Path $root 'idm.c') /Fe:(Join-Path $out 'light-downloader.exe') /link winhttp.lib
if ($LASTEXITCODE) { throw 'idm.c compilation failed' }

& $clang /nologo /O2 /W4 /EHsc /std:c++17 /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 @sdkFlags `
  (Join-Path $root 'idm_gui.cpp') /Fe:(Join-Path $out 'LightDownloader.exe') /link comctl32.lib comdlg32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib
if ($LASTEXITCODE) { throw 'idm_gui.cpp compilation failed' }
Write-Host "Built $out\light-downloader.exe and $out\LightDownloader.exe with LLVM clang-cl"