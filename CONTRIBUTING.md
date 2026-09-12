# Contributing

Issues and pull requests are welcome. Please describe the platform, compiler,
download URL behavior, and exact reproduction steps for bugs.

## Development

The current implementation is a native Windows application. Build it with
LLVM/Clang and the Windows SDK:

```powershell
.\build-llvm.ps1
```

Do not add generator-based build files. Keep the direct compiler scripts portable,
deterministic, and explicit about required SDKs. Test worker resume, range
fallback, pause, retries, and GUI process discovery before submitting changes.

## Scope

New platform backends are welcome, but must use platform-native networking and
provide equivalent persistence and resume behavior before being advertised as
supported. Do not claim macOS/Linux support based only on packaging a Windows
binary.
