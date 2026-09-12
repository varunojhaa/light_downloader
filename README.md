# Light Downloader

Lightweight IDM-style native download manager. The default backend is the C
WinHTTP worker because it is the lightest option: it uses Windows networking,
bounded 64 KiB buffers, one thread per configured connection, and no runtime
or third-party DLLs. The Win32 GUI is written in C++17. A Rust worker is kept
under `rust-core` as an optional backend.

## Implemented core behavior

- HTTP and HTTPS downloads through WinHTTP, including system proxy handling.
- Up to 16 concurrent byte ranges with retry/backoff.
- Resume using `.part` files and `.idm` segment metadata.
- Atomic metadata writes and safe final replacement.
- Pause/resume from the GUI or Ctrl+C in the worker.
- Progress, size, speed, and connection count in the GUI.
- Per-transfer bandwidth limit (`--limit`), retry count, and range fallback.
- GUI process isolation: each active transfer has its own worker process.

## LLVM build

LLVM 22.1.8 is expected at `C:\Program Files\LLVM\bin`. Run PowerShell from
this directory. A Visual Studio Developer PowerShell is recommended so the
Windows SDK libraries are discoverable:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build-llvm.ps1
build-llvm\LightDownloader.exe
```

This creates `build-llvm\light-downloader.exe` and `build-llvm\LightDownloader.exe` with
`clang-cl.exe`. The GUI launches `light-downloader.exe` by default. To explicitly use the
Rust worker, place `light-downloader-rust.exe` beside the GUI and set `$env:IDM_BACKEND =
'rust'`.

The compiler command is kept in `build-llvm.ps1` so LLVM users can build without
an IDE extension or generator.

## Worker usage
```powershell
build-llvm\light-downloader.exe https://example.com/file.iso
build-llvm\light-downloader.exe https://example.com/file.iso D:\Downloads\file.iso --connections 8
build-llvm\light-downloader.exe --help
build-llvm\light-downloader.exe --help
```

State is saved beside the output as `<output>.part` and `<output>.idm`; rerun
the same command after a pause or interruption to resume.

## AB Download Manager compatibility scope

The AB Download Manager source was reviewed across its downloader core,
HTTP/HLS/DASH jobs, queue manager, persistence, checksum screens, proxy/DNS
settings, scheduling, browser/integration server, notifications, and platform
UI. This repository currently implements the low-resource desktop HTTP(S)
core and IDM-like GUI. HLS/DASH parsing, checksum verification UI, scheduled
queues, authentication editor, browser extension protocol, embedded API/server,
notifications, and platform-specific settings are not yet implemented here;
they require additional protocol and UI code and should not be represented as
complete merely by matching the downloader executable.

The Rust prototype uses the same `.part`/`.idm` concept but is optional. The C
worker is the recommended default for low memory and low CPU usage.

## Installers and platform support

The current source is a native Windows application because it uses Win32 and
WinHTTP. Windows builds can be packaged with Inno Setup:

```powershell
.\build-llvm.ps1
iscc packaging\windows\light-downloader.iss
```

`packaging/unix/package.sh` packages a platform-native binary supplied by a
future macOS/Linux backend as a macOS app/DMG or Linux tarball/DEB. It does not
convert the Windows executable into a Unix application. macOS and Linux
download-manager backends are not implemented yet; adding those backends is
required before those installers can contain working native applications.

## Open source and GitHub

This project is released under the MIT License. To publish it, create an empty
public repository on GitHub and run:

```powershell
.\scripts\publish-github.ps1
git remote add origin https://github.com/YOUR-USER/light-downloader.git
git branch -M main
git push -u origin main
```