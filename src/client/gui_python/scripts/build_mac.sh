#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="$SCRIPT_DIR/dist/mac"

echo "=== NEVO Mac Client Build Script ==="
echo ""

check_command() {
    if ! command -v "$1" &> /dev/null; then
        echo "[ERROR] $1 not found. Please install it first."
        if [ -n "${2:-}" ]; then
            echo "  Hint: $2"
        fi
        exit 1
    fi
}

PYTHON_CMD=""
for candidate in python3.12 python3.11 python3.10 python3.9 python3; do
    if command -v "$candidate" &> /dev/null; then
        ver=$("$candidate" -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
        IFS='.' read -r major minor <<< "$ver"
        if [ "$major" -ge 3 ] && [ "$minor" -ge 9 ]; then
            PYTHON_CMD="$candidate"
            break
        fi
    fi
done

if [ -z "$PYTHON_CMD" ]; then
    echo "[ERROR] Python 3.9+ not found. Please install Python 3.9 or later."
    exit 1
fi

echo "[1/5] Checking dependencies..."

check_command "$PYTHON_CMD" "brew install python@3.11"
check_command "git" "xcode-select --install"
check_command "xcode-select" "xcode-select --install"

if ! command -v brew &> /dev/null; then
    echo "[WARN] Homebrew not found. Some dependencies may need manual install."
    echo "  Install: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    BREW_MISSING=true
else
    BREW_MISSING=false
fi

if [ "$BREW_MISSING" = false ]; then
    echo "[2/5] Installing system dependencies via Homebrew..."
    brew install opus libsodium portaudio || true
else
    echo "[2/5] Skipping Homebrew install (not available)"
fi

echo "[3/5] Installing Python dependencies..."
"$PYTHON_CMD" -m pip install --upgrade pip

"$PYTHON_CMD" -m pip install \
    pyqt5 \
    qfluentwidgets \
    sounddevice \
    numpy \
    scipy \
    noisereduce \
    av \
    requests \
    pynput \
    protobuf \
    mss \
    pyobjc-framework-Quartz \
    pyobjc-framework-Cocoa \
    pyobjc-framework-CoreGraphics \
    pyobjc-framework-ApplicationServices

"$PYTHON_CMD" -m pip install opuslib 2>/dev/null || {
    echo "[WARN] opuslib not available via pip. Will try to build from source or use fallback."
    if command -v brew &> /dev/null; then
        brew install opus
    fi
}

"$PYTHON_CMD" -m pip install pyinstaller

"$PYTHON_CMD" -c "import mss" || { echo "[ERROR] mss import failed"; exit 1; }
"$PYTHON_CMD" -c "import PyQt5" || { echo "[ERROR] PyQt5 import failed"; exit 1; }
"$PYTHON_CMD" -c "import Quartz" || { echo "[ERROR] Quartz (pyobjc) import failed"; exit 1; }

echo "[4/5] Building Mac app bundle..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

cd "$CLIENT_DIR"

"$PYTHON_CMD" -m PyInstaller nevo_client_mac.spec \
    --distpath "$DIST_DIR" \
    --workpath "$DIST_DIR/build" \
    --clean \
    --noconfirm

echo "[5/5] Verifying build..."
APP_PATH="$DIST_DIR/NEVO.app"
if [ -d "$APP_PATH" ]; then
    echo ""
    echo "=== Build Successful ==="
    echo "App: $APP_PATH"
    echo ""
    echo "To run: open \"$APP_PATH\""
    echo ""
    echo "To create DMG:"
    echo "  mkdir -p /tmp/nevo-dmg"
    echo "  cp -R \"$APP_PATH\" /tmp/nevo-dmg/"
    echo "  ln -s /Applications /tmp/nevo-dmg/Applications"
    echo "  hdiutil create -volname NEVO -srcfolder /tmp/nevo-dmg -ov -format UDZO \"$DIST_DIR/NEVO.dmg\""
    echo "  rm -rf /tmp/nevo-dmg"
else
    echo ""
    echo "[ERROR] Build failed: .app bundle not found"
    echo "Check build logs above for details."
    exit 1
fi