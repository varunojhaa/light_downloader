; Build with Inno Setup 6 after running build-llvm.ps1.
#define AppName "IDM-C"
#define AppVersion "1.0.0"
#define AppPublisher "IDM-C contributors"
#define AppExeName "IDM-C.exe"

[Setup]
AppId={{7E8D2F3A-0B44-4A41-B7CF-3B0AA7E9C001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\IDM-C
DefaultGroupName=IDM-C
OutputDir=..\..\dist
OutputBaseFilename=IDM-C-{#AppVersion}-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\..\LICENSE

[Files]
Source: "..\..\build-llvm\IDM-C.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\build-llvm\idm.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\IDM-C"; Filename: "{app}\IDM-C.exe"
Name: "{autodesktop}\IDM-C"; Filename: "{app}\IDM-C.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
