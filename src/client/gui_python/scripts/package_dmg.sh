#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$SCRIPT_DIR/../dist/mac"
APP_PATH="$DIST_DIR/NEVO.app"
DMG_PATH="$DIST_DIR/NEVO-Client.dmg"

if [ ! -d "$APP_PATH" ]; then
    echo "[ERROR] NEVO.app not found at $APP_PATH"
    echo "Run build_mac.sh first."
    exit 1
fi

echo "=== Creating NEVO DMG Installer ==="

TMP_DIR="$(mktemp -d)"
trap "rm -rf $TMP_DIR" EXIT

DMG_CONTENT="$TMP_DIR/dmg-content"
mkdir -p "$DMG_CONTENT"

cp -R "$APP_PATH" "$DMG_CONTENT/"

ln -s /Applications "$DMG_CONTENT/Applications"

rm -f "$DMG_PATH"
echo "Writing DMG..."
hdiutil create \
    -volname "NEVO" \
    -srcfolder "$DMG_CONTENT" \
    -ov -format UDZO \
    "$DMG_PATH"

DMG_SIZE=$(du -h "$DMG_PATH" | cut -f1)
echo ""
echo "=== DMG Created Successfully ==="
echo "File: $DMG_PATH"
echo "Size: $DMG_SIZE"