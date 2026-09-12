; Build with Inno Setup 6 after running build-llvm.ps1.
#define AppName "Light Downloader"
#define AppVersion "1.0.0"
#define AppPublisher "Light Downloader contributors"
#define AppExeName "LightDownloader.exe"

[Setup]
AppId={{7E8D2F3A-0B44-4A41-B7CF-3B0AA7E9C001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\Light Downloader
DefaultGroupName=Light Downloader
OutputDir=..\..\dist
OutputBaseFilename=Light-Downloader-{#AppVersion}-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\..\LICENSE

[Files]
Source: "..\..\build-llvm\LightDownloader.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\build-llvm\light-downloader.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Light Downloader"; Filename: "{app}\LightDownloader.exe"
Name: "{autodesktop}\Light Downloader"; Filename: "{app}\LightDownloader.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
