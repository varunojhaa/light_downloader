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
    APPDIR="$OUT/IDM-C.app/Contents/MacOS"
    mkdir -p "$APPDIR"
    cp "$APP" "$APPDIR/IDM-C"
    chmod +x "$APPDIR/IDM-C"
    cat > "$OUT/IDM-C.app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleName</key><string>IDM-C</string><key>CFBundleIdentifier</key><string>org.idmc.downloadmanager</string><key>CFBundleVersion</key><string>$VERSION</string><key>CFBundleExecutable</key><string>IDM-C</string></dict></plist>
EOF
    if command -v hdiutil >/dev/null 2>&1; then hdiutil create -volname IDM-C -srcfolder "$OUT/IDM-C.app" -ov -format UDZO "$OUT/IDM-C-$VERSION.dmg"; fi
    ;;
  Linux)
    STAGE="$OUT/idm-c-$VERSION-linux"
    rm -rf "$STAGE"; mkdir -p "$STAGE/usr/bin" "$STAGE/usr/share/applications"
    cp "$APP" "$STAGE/usr/bin/idm-c"; chmod +x "$STAGE/usr/bin/idm-c"
    printf '[Desktop Entry]\nType=Application\nName=IDM-C\nExec=idm-c\nTerminal=false\nCategories=Network;FileTransfer;\n' > "$STAGE/usr/share/applications/idm-c.desktop"
    tar -C "$OUT" -czf "$OUT/idm-c-$VERSION-linux.tar.gz" "idm-c-$VERSION-linux"
    if command -v dpkg-deb >/dev/null 2>&1; then
      DEB="$OUT/deb"; rm -rf "$DEB"; mkdir -p "$DEB/DEBIAN" "$DEB/usr/bin" "$DEB/usr/share/applications"
      cp "$APP" "$DEB/usr/bin/idm-c"; chmod +x "$DEB/usr/bin/idm-c"; cp "$STAGE/usr/share/applications/idm-c.desktop" "$DEB/usr/share/applications/"
      printf 'Package: idm-c\nVersion: %s\nArchitecture: amd64\nMaintainer: IDM-C contributors\nDescription: Lightweight download manager\n' "$VERSION" > "$DEB/DEBIAN/control"
      dpkg-deb --build "$DEB" "$OUT/idm-c-$VERSION-amd64.deb" >/dev/null
    fi
    ;;
  *) echo "Unsupported host: use this script on macOS or Linux" >&2; exit 1;;
esac
