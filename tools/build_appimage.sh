#!/usr/bin/env bash
# build_appimage.sh — Build a Linux AppImage for BANDPASS II
#
# Prerequisites:
#   sudo apt-get install libwxgtk3.2-dev libwxgtk-webview3.2-dev \
#        libgdal-dev libsqlite3-dev libcurl4-openssl-dev cmake ninja-build
#
# Usage:
#   cd <repo root>
#   bash tools/build_appimage.sh [--build-dir build-appimage] [--arch x86_64]
#
# Outputs:
#   BANDPASS_II-<version>-<arch>.AppImage   in the current directory

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build-appimage"
ARCH="${ARCH:-x86_64}"
VERSION="${VERSION:-$(git -C "$REPO_ROOT" describe --tags --always 2>/dev/null || echo dev)}"
APPIMAGE_NAME="BANDPASS_II-${VERSION}-${ARCH}.AppImage"

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --arch)      ARCH="$2";      shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

echo "=== BANDPASS II AppImage builder ==="
echo "Repo:      $REPO_ROOT"
echo "Build dir: $BUILD_DIR"
echo "Arch:      $ARCH"
echo "Version:   $VERSION"
echo ""

# ---- 1. Download linuxdeploy + appimagetool if not present ----
TOOLS_DIR="$REPO_ROOT/.appimage-tools"
mkdir -p "$TOOLS_DIR"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_WX="$TOOLS_DIR/linuxdeploy-plugin-wx.sh"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-${ARCH}.AppImage"

if [[ ! -x "$LINUXDEPLOY" ]]; then
    echo "Downloading linuxdeploy..."
    curl -sSfL -o "$LINUXDEPLOY" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
    chmod +x "$LINUXDEPLOY"
fi

if [[ ! -x "$APPIMAGETOOL" ]]; then
    echo "Downloading appimagetool..."
    curl -sSfL -o "$APPIMAGETOOL" \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${ARCH}.AppImage"
    chmod +x "$APPIMAGETOOL"
fi

# The wxWidgets linuxdeploy plugin bundles the wx shared libraries.
if [[ ! -f "$LINUXDEPLOY_WX" ]]; then
    echo "Downloading linuxdeploy-plugin-wx..."
    curl -sSfL -o "$LINUXDEPLOY_WX" \
        "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-wx/master/linuxdeploy-plugin-wx.sh"
    chmod +x "$LINUXDEPLOY_WX"
fi

# ---- 2. Build BANDPASS II in Release mode ----
echo ""
echo "=== Configuring and building BANDPASS II ==="
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/AppDir/usr"

cmake --build "$BUILD_DIR" -j"$(nproc)"
cmake --install "$BUILD_DIR"

# ---- 3. Bundle web assets into AppDir ----
APPDIR="$BUILD_DIR/AppDir"
WEB_DEST="$APPDIR/usr/share/bandpass2/web"
mkdir -p "$WEB_DEST"
cp -r "$REPO_ROOT/src/web/"* "$WEB_DEST/"

# Zone data
ZONE_DEST="$APPDIR/usr/share/bandpass2/zones"
mkdir -p "$ZONE_DEST"
cp "$REPO_ROOT/data/zones/"*.geojson "$ZONE_DEST/" 2>/dev/null || true

# ---- 4. Desktop file + icon ----
APPS_DIR="$APPDIR/usr/share/applications"
ICON_DIR="$APPDIR/usr/share/icons/hicolor/128x128/apps"
mkdir -p "$APPS_DIR" "$ICON_DIR"

cp "$REPO_ROOT/data/bandpass2.desktop" "$APPS_DIR/"

# Generate a simple placeholder icon if none exists
ICON_SRC="$REPO_ROOT/data/bandpass2.png"
if [[ ! -f "$ICON_SRC" ]]; then
    echo "No icon found at $ICON_SRC — generating placeholder using ImageMagick (if available)"
    if command -v convert &>/dev/null; then
        convert -size 128x128 xc:'#1a5276' \
                -fill white -pointsize 18 \
                -gravity center -annotate 0 'BANDPASS II' \
                "$ICON_SRC"
    else
        echo "ImageMagick not available — skipping icon generation."
        echo "Provide data/bandpass2.png (128×128 PNG) for a proper icon."
    fi
fi
[[ -f "$ICON_SRC" ]] && cp "$ICON_SRC" "$ICON_DIR/bandpass2.png"

# ---- 5. AppRun wrapper ----
# Sets XDG_DATA_DIRS so the app can find its bundled web assets + zones.
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "$0")")"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export BANDPASS2_DATA_DIR="$HERE/usr/share/bandpass2"
exec "$HERE/usr/bin/bandpass2" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# ---- 6. Bundle shared libraries with linuxdeploy ----
echo ""
echo "=== Bundling shared libraries ==="
LINUXDEPLOY_PLUGIN_WX="$LINUXDEPLOY_WX" \
    "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/bandpass2" \
    --desktop-file "$APPS_DIR/bandpass2.desktop" \
    --plugin wx \
    --output appimage

# Rename to include version and arch
if [[ -f "BANDPASS_II-${ARCH}.AppImage" ]]; then
    mv "BANDPASS_II-${ARCH}.AppImage" "$APPIMAGE_NAME"
fi

echo ""
echo "=== Done: $APPIMAGE_NAME ==="
