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
  Linux)
    STAGE="$OUT/light-downloader-$VERSION-linux"
    rm -rf "$STAGE"; mkdir -p "$STAGE/usr/bin" "$STAGE/usr/share/applications"
    cp "$APP" "$STAGE/usr/bin/light-downloader"; chmod +x "$STAGE/usr/bin/light-downloader"
    printf '[Desktop Entry]\nType=Application\nName=Light Downloader\nExec=light-downloader\nTerminal=false\nCategories=Network;FileTransfer;\n' > "$STAGE/usr/share/applications/light-downloader.desktop"
    tar -C "$OUT" -czf "$OUT/Light-Downloader-$VERSION-linux.tar.gz" "light-downloader-$VERSION-linux"
    if command -v dpkg-deb >/dev/null 2>&1 && [ "$(uname -m)" = "x86_64" ]; then
      DEB="$OUT/deb"; rm -rf "$DEB"; mkdir -p "$DEB/DEBIAN" "$DEB/usr/bin" "$DEB/usr/share/applications"
      cp "$APP" "$DEB/usr/bin/light-downloader"; chmod +x "$DEB/usr/bin/light-downloader"; cp "$STAGE/usr/share/applications/light-downloader.desktop" "$DEB/usr/share/applications/"
      printf 'Package: light-downloader\nVersion: %s\nArchitecture: amd64\nMaintainer: Light Downloader contributors\nDescription: Lightweight download manager\n' "$VERSION" > "$DEB/DEBIAN/control"
      dpkg-deb --build "$DEB" "$OUT/Light-Downloader-$VERSION-amd64.deb" >/dev/null
    fi
    ;;
  *) echo "Unsupported host: use this script on Linux" >&2; exit 1;;
esac
