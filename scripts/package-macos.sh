#!/usr/bin/env bash
# Stage Radiacode Monitor .app with Qt + QtRadiacode + libusb (macdeployqt).
#
# Usage (from repo root or anywhere):
#   ./scripts/package-macos.sh
#   ./scripts/package-macos.sh --bin-dir build/Qt_6_11_1_for_macOS_Release/bin
#   ./scripts/package-macos.sh --qt-dir "$HOME/Qt/6.11.1/macos"
#   ./scripts/package-macos.sh --version 0.2.1   # optional override
#   ./scripts/package-macos.sh --dmg          # also create a simple DMG
#   ./scripts/package-macos.sh --skip-deploy  # only re-stage / re-sign
#
# Version defaults to project(... VERSION x.y.z) in CMakeLists.txt.
#
# Output:
#   dist/Radiacode Monitor.app
#   dist/RadiacodeMonitor-<version>-macos.dmg   (with --dmg)
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION=""
BIN_DIR=""
QT_DIR=""
DO_DMG=0
SKIP_DEPLOY=0
APP_NAME="Radiacode Monitor"
BUNDLE_ID="io.qtradiacode.radiacode-monitor"

usage() {
  sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

read_project_version() {
  local cmake="$REPO_ROOT/CMakeLists.txt"
  [[ -f "$cmake" ]] || { echo "CMakeLists.txt not found: $cmake" >&2; exit 1; }
  # project(radiacode-monitor VERSION 0.2.0 LANGUAGES CXX)
  local ver
  ver="$(sed -nE 's/.*project[[:space:]]*\([[:space:]]*radiacode-monitor[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$cmake" | head -1)"
  if [[ -z "$ver" ]]; then
    echo "Could not parse project VERSION from CMakeLists.txt" >&2
    exit 1
  fi
  echo "$ver"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin-dir) BIN_DIR="$2"; shift 2 ;;
    --qt-dir) QT_DIR="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --dmg) DO_DMG=1; shift ;;
    --skip-deploy) SKIP_DEPLOY=1; shift ;;
    -h|--help) usage 0 ;;
    *) echo "Unknown option: $1" >&2; usage 1 ;;
  esac
done

if [[ -z "$VERSION" ]]; then
  VERSION="$(read_project_version)"
elif [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid --version '$VERSION' (expected x.y.z)" >&2
  exit 1
fi

find_bin_dir() {
  if [[ -n "$BIN_DIR" ]]; then
    [[ -d "$BIN_DIR/radiacode-monitor.app" ]] || {
      echo "No radiacode-monitor.app in --bin-dir=$BIN_DIR" >&2
      exit 1
    }
    echo "$(cd "$BIN_DIR" && pwd)"
    return
  fi
  local candidates=(
    "$REPO_ROOT/build/Qt_6_11_1_for_macOS_Release/bin"
    "$REPO_ROOT/build/mac-test/bin"
    "$REPO_ROOT/build/bin"
    "$REPO_ROOT/build/Release/bin"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -d "$c/radiacode-monitor.app" ]]; then
      echo "$(cd "$c" && pwd)"
      return
    fi
  done
  if [[ -d "$REPO_ROOT/build" ]]; then
    local found
    found="$(find "$REPO_ROOT/build" -type d -name 'radiacode-monitor.app' 2>/dev/null | head -1 || true)"
    if [[ -n "$found" ]]; then
      echo "$(cd "$(dirname "$found")" && pwd)"
      return
    fi
  fi
  echo "radiacode-monitor.app not found. Build Release first or pass --bin-dir." >&2
  exit 1
}

find_qt_dir() {
  if [[ -n "$QT_DIR" ]]; then
    [[ -x "$QT_DIR/bin/macdeployqt" ]] || {
      echo "macdeployqt not found under --qt-dir=$QT_DIR" >&2
      exit 1
    }
    echo "$(cd "$QT_DIR" && pwd)"
    return
  fi
  if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    local first="${CMAKE_PREFIX_PATH%%:*}"
    if [[ -x "$first/bin/macdeployqt" ]]; then
      echo "$(cd "$first" && pwd)"
      return
    fi
  fi
  local candidates=(
    "$HOME/Qt/6.11.1/macos"
    "$HOME/Qt/6.10.0/macos"
    "$HOME/Qt/6.9.0/macos"
    /Users/martinpetriska/Qt/6.11.1/macos
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -x "$c/bin/macdeployqt" ]]; then
      echo "$(cd "$c" && pwd)"
      return
    fi
  done
  # Last resort: locate macdeployqt on PATH
  if command -v macdeployqt >/dev/null 2>&1; then
    echo "$(cd "$(dirname "$(command -v macdeployqt)")/.." && pwd)"
    return
  fi
  echo "Qt macos kit not found. Pass --qt-dir \$HOME/Qt/6.x/macos" >&2
  exit 1
}

BIN_DIR="$(find_bin_dir)"
QT_DIR="$(find_qt_dir)"
MACDEPLOYQT="$QT_DIR/bin/macdeployqt"

DIST_DIR="$REPO_ROOT/dist"
STAGE_DIR="$DIST_DIR/stage"
STAGE_APP="$STAGE_DIR/${APP_NAME}.app"
SRC_APP="$BIN_DIR/radiacode-monitor.app"
OUT_APP="$DIST_DIR/${APP_NAME}.app"
DMG_PATH="$DIST_DIR/RadiacodeMonitor-${VERSION}-macos.dmg"

echo "==> Source app:  $SRC_APP"
echo "==> Qt:          $QT_DIR"
echo "==> Version:     $VERSION"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
# Fresh copy of the CMake-built bundle
cp -R "$SRC_APP" "$STAGE_APP"

# macdeployqt looks for extra dylibs next to the binary / via -libpath
# QtRadiacode is built into BIN_DIR as libQtRadiacode*.dylib
if [[ $SKIP_DEPLOY -eq 0 ]]; then
  echo "==> macdeployqt (Qt frameworks + plugins + dependent dylibs)…"
  "$MACDEPLOYQT" "$STAGE_APP" \
    -verbose=1 \
    -libpath="$BIN_DIR" \
    -executable="$STAGE_APP/Contents/MacOS/radiacode-monitor" \
    || {
      echo "macdeployqt failed" >&2
      exit 1
    }
else
  echo "==> Skipping macdeployqt (--skip-deploy)"
fi

# Ensure app display name matches (Finder / Dock)
/usr/libexec/PlistBuddy -c "Set :CFBundleName ${APP_NAME}" \
  "$STAGE_APP/Contents/Info.plist" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName ${APP_NAME}" \
  "$STAGE_APP/Contents/Info.plist" 2>/dev/null \
  || /usr/libexec/PlistBuddy -c "Add :CFBundleDisplayName string ${APP_NAME}" \
       "$STAGE_APP/Contents/Info.plist" 2>/dev/null || true

# Ad-hoc sign so the staged app can be launched outside the build tree.
# For distribution outside your machine you need an Apple Developer ID + notarization.
echo "==> codesign (ad-hoc)…"
codesign --force --deep --sign - \
  --identifier "$BUNDLE_ID" \
  --entitlements /dev/null \
  "$STAGE_APP" 2>/dev/null \
  || codesign --force --deep --sign - "$STAGE_APP"

# Publish to dist/
rm -rf "$OUT_APP"
mkdir -p "$DIST_DIR"
cp -R "$STAGE_APP" "$OUT_APP"

echo "==> App bundle: $OUT_APP"
echo "    size: $(du -sh "$OUT_APP" | awk '{print $1}')"

# Quick load check (non-GUI: just dyld resolve)
if ! otool -L "$OUT_APP/Contents/MacOS/radiacode-monitor" | head -5 >/dev/null; then
  echo "warning: otool failed on packaged binary" >&2
fi

if [[ $DO_DMG -eq 1 ]]; then
  echo "==> Creating DMG…"
  rm -f "$DMG_PATH"
  # Staging folder for a cleaner DMG layout
  DMG_STAGE="$DIST_DIR/dmg-root"
  rm -rf "$DMG_STAGE"
  mkdir -p "$DMG_STAGE"
  cp -R "$OUT_APP" "$DMG_STAGE/"
  ln -s /Applications "$DMG_STAGE/Applications"
  hdiutil create -volname "${APP_NAME}" \
    -srcfolder "$DMG_STAGE" \
    -ov -format UDZO \
    "$DMG_PATH"
  rm -rf "$DMG_STAGE"
  echo "==> DMG: $DMG_PATH"
fi

echo ""
echo "Done."
echo "  Open:  open \"$OUT_APP\""
echo "  Copy to /Applications if you like."
echo ""
echo "Note: ad-hoc signature is fine on your Mac. For other users / Gatekeeper,"
echo "sign with a Developer ID and notarize (not covered by this script)."
