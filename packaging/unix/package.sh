#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/dist"
APP="$1"
VERSION="${2:-1.0.0}"

if [ ! -f "$APP" ]; then
    echo "Usage: $0 PATH_TO_PLATFORM_BINARY [VERSION]" >&2
    exit 2
fi
mkdir -p "$OUT"
case "$(uname -s)" in
  Darwin)
    APPDIR="$OUT/Light Downloader.app/Contents/MacOS"
    mkdir -p "$APPDIR"
    cp "$APP" "$APPDIR/LightDownloader"
    chmod +x "$APPDIR/LightDownloader"
    cat > "$OUT/Light Downloader.app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleName</key><string>Light Downloader</string><key>CFBundleIdentifier</key><string>org.lightdownloader.app</string><key>CFBundleVersion</key><string>$VERSION</string><key>CFBundleExecutable</key><string>LightDownloader</string></dict></plist>
EOF
    if command -v hdiutil >/dev/null 2>&1; then hdiutil create -volname "Light Downloader" -srcfolder "$OUT/Light Downloader.app" -ov -format UDZO "$OUT/Light-Downloader-$VERSION.dmg"; fi
    ;;
  Linux)
    STAGE="$OUT/light-downloader-$VERSION-linux"
    rm -rf "$STAGE"; mkdir -p "$STAGE/usr/bin" "$STAGE/usr/share/applications"
    cp "$APP" "$STAGE/usr/bin/light-downloader"; chmod +x "$STAGE/usr/bin/light-downloader"
    printf '[Desktop Entry]\nType=Application\nName=Light Downloader\nExec=light-downloader\nTerminal=false\nCategories=Network;FileTransfer;\n' > "$STAGE/usr/share/applications/light-downloader.desktop"
    tar -C "$OUT" -czf "$OUT/Light-Downloader-$VERSION-linux.tar.gz" "light-downloader-$VERSION-linux"
    if command -v dpkg-deb >/dev/null 2>&1; then
      DEB="$OUT/deb"; rm -rf "$DEB"; mkdir -p "$DEB/DEBIAN" "$DEB/usr/bin" "$DEB/usr/share/applications"
      cp "$APP" "$DEB/usr/bin/light-downloader"; chmod +x "$DEB/usr/bin/light-downloader"; cp "$STAGE/usr/share/applications/light-downloader.desktop" "$DEB/usr/share/applications/"
      printf 'Package: light-downloader\nVersion: %s\nArchitecture: amd64\nMaintainer: Light Downloader contributors\nDescription: Lightweight download manager\n' "$VERSION" > "$DEB/DEBIAN/control"
      dpkg-deb --build "$DEB" "$OUT/Light-Downloader-$VERSION-amd64.deb" >/dev/null
    fi
    ;;
  *) echo "Unsupported host: use this script on macOS or Linux" >&2; exit 1;;
esac
