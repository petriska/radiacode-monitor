#!/usr/bin/env bash
# Build a Debian package (amd64/arm64/…) with CPack.
#
# Usage (from repo root or anywhere):
#   ./scripts/package-deb.sh
#   ./scripts/package-deb.sh --build-dir build-deb
#   ./scripts/package-deb.sh --version 0.2.1   # optional override of package version
#   ./scripts/package-deb.sh --clean          # remove build dir first
#
# Requirements (Ubuntu 24.04 example):
#   sudo apt install build-essential cmake ninja-build \
#     qt6-base-dev qt6-connectivity-dev libusb-1.0-0-dev \
#     dpkg-dev file
#
# Expects a local sibling qtradiacode tree (../qtradiacode) or FetchContent.
# Version defaults to project(... VERSION x.y.z) in CMakeLists.txt.
#
# Output:
#   dist/radiacode-monitor_<version>_<arch>.deb
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION=""
BUILD_DIR=""
CLEAN=0
GENERATOR=""

usage() {
  sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

read_project_version() {
  local cmake="$REPO_ROOT/CMakeLists.txt"
  [[ -f "$cmake" ]] || { echo "CMakeLists.txt not found: $cmake" >&2; exit 1; }
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
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    --generator) GENERATOR="$2"; shift 2 ;;
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

if [[ -z "$BUILD_DIR" ]]; then
  BUILD_DIR="$REPO_ROOT/build-deb"
fi
# Resolve relative --build-dir against cwd
if [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="$(pwd)/$BUILD_DIR"
fi

if [[ "$CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
  echo "Removing $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
DIST_DIR="$REPO_ROOT/dist"
mkdir -p "$DIST_DIR"

# Prefer Ninja when available
if [[ -z "$GENERATOR" ]]; then
  if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
  else
    GENERATOR="Unix Makefiles"
  fi
fi

echo "Configure: prefix=/usr  version=$VERSION  generator=$GENERATOR"
echo "Build dir: $BUILD_DIR"

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
  -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_TESTS=OFF \
  -DCPACK_PACKAGE_VERSION="$VERSION"

cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 4)"

echo "CPack DEB…"
(
  cd "$BUILD_DIR"
  cpack -G DEB
)

shopt -s nullglob
debs=( "$BUILD_DIR"/radiacode-monitor_*.deb "$BUILD_DIR"/*.deb )
if [[ ${#debs[@]} -eq 0 ]]; then
  echo "No .deb produced in $BUILD_DIR" >&2
  exit 1
fi

# Prefer DEB-DEFAULT name; copy all matching debs to dist/
copied=""
for deb in "${debs[@]}"; do
  base="$(basename "$deb")"
  # Skip intermediate CPack junk if any
  [[ "$base" == radiacode-monitor* ]] || continue
  cp -f "$deb" "$DIST_DIR/"
  echo "  -> $DIST_DIR/$base"
  copied=1
done

if [[ -z "$copied" ]]; then
  # Fallback: first deb
  cp -f "${debs[0]}" "$DIST_DIR/"
  echo "  -> $DIST_DIR/$(basename "${debs[0]}")"
fi

echo
echo "Install (local):"
echo "  sudo apt install ./dist/radiacode-monitor_${VERSION}_*.deb"
echo "  # or: sudo dpkg -i dist/radiacode-monitor_*.deb && sudo apt-get install -f"
echo
echo "After install: unplug/replug USB detector (udev rules reloaded by postinst)."
