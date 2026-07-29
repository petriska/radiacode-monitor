# radiacode-monitor

Simple **Qt 6 Widgets** desktop app for [RadiaCode](https://www.radiacode.com/) detectors using the **QtRadiacode** library.

> **Unofficial community software.** Not affiliated with, endorsed by, or sponsored by the RadiaCode hardware manufacturer.

## Features (MVP)

- Device list: **USB** + **BLE** (Refresh scans both)
- Connect / disconnect (USB by serial, BLE by address)
- Live count rate and dose rate (1 s poll)
- Live **battery** (% when device reports Rare DATA_BUF) and **BLE signal** (RSSI from scan; live RSSI needs Qt 6.5+)
- Interactive spectrum plot (channels / keV, √ Y-scale)
  - **Wheel**: zoom X · **Drag**: pan · **Double-click**: reset view
  - **Cursor**: vertical line with energy (keV), channel, and counts
- **ROI time series** tab — universal energy-window rates vs time
  - Presets (e.g. radon daughters ²¹⁴Pb / ²¹⁴Bi) or **custom ROIs** (add/edit/remove)
  - Dwell sampling, **t₀** marker, multi-curve chart, **CSV export**
- Temperature / charge when present in DATA_BUF

## Dependencies

- Qt 6 Core + Widgets
- [qtradiacode](../qtradiacode) library (sibling directory by default)
- libusb-1.0 (via QtRadiacode)

## Build

**Requirements:** CMake 3.21+, Qt 6 (Core + Widgets), C++17 compiler, libusb-1.0 (via QtRadiacode).

### From Git clone (developers)

```bash
# Recommended layout:
#   workspace/qtradiacode/       # library
#   workspace/radiacode-monitor/ # this app

git clone https://github.com/petriska/qtradiacode.git
git clone https://github.com/petriska/radiacode-monitor.git
cd radiacode-monitor
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x
cmake --build .
```

If the library is elsewhere:

```bash
cmake .. -DQTRADIACODE_DIR=/path/to/qtradiacode -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x
```

If **no** local `qtradiacode` tree is found, CMake **downloads it automatically**
via FetchContent (`QTRADIACODE_GIT_TAG`, default `v0.1.3` — needs network + git).

### From GitHub Release ZIP (no git clone)

1. On [qtradiacode Releases](https://github.com/petriska/qtradiacode/releases) download
   **Source code (zip)** and extract it (folder is usually `qtradiacode-0.1.3` or similar).
2. On [radiacode-monitor Releases](https://github.com/petriska/radiacode-monitor/releases)
   download **Source code (zip)** and extract it next to the library.
3. Point CMake at the library, or rename the library folder to `qtradiacode` as a sibling:

```text
parent/
  qtradiacode/              # or: qtradiacode-0.1.3  (auto-detected)
  radiacode-monitor-0.2.2/  # extracted app sources
```

```bash
cd radiacode-monitor-0.2.2   # extracted app folder
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x
# if auto-detect fails:
# cmake .. -DQTRADIACODE_DIR=../qtradiacode-0.1.3 -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x
cmake --build .
```

You can also extract **only** the monitor ZIP and let FetchContent pull the library (needs network).

## App icon

Shared artwork (radiation + spectrum):

| Path | Use |
|------|-----|
| `icons/radiacode-monitor-1024.png` | Master source |
| `icons/radiacode-monitor.ico` | Windows `.exe` + NSIS installer |
| `icons/radiacode-monitor-{16…512}.png` | Linux hicolor theme |
| `icons/radiacode-monitor.png` | Generic 256×256 |
| `macos/radiacode-monitor.icns` | macOS Dock / Finder |
| `resources/app.qrc` | Window / taskbar icon (all platforms via `QIcon`) |

Windows embeds the `.ico` through `windows/radiacode-monitor.rc`. Linux installs a `.desktop` file (`linux/radiacode-monitor.desktop.in`) and hicolor icons on `cmake --install`.

## Windows installer (NSIS)

After a **Release** build (shared `QtRadiacode.dll` + `libusb-1.0.dll` next to the exe):

**Requirements:** [NSIS 3.x](https://nsis.sourceforge.io/), Qt kit with `windeployqt` / `windeployqt6`.

```powershell
# From the radiacode-monitor repo root
# Version is read automatically from CMakeLists.txt (project(... VERSION x.y.z))
.\scripts\package-windows.ps1

# Or with explicit paths / optional version override:
.\scripts\package-windows.ps1 `
  -BuildBinDir .\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Release\bin `
  -QtDir M:\Qt\6.11.1\msvc2022_64 `
  -Version 0.2.2   # optional; omit to use CMakeLists.txt
```

This will:

1. Resolve **version** from `CMakeLists.txt` (unless `-Version` is passed)
2. Stage files under `dist\stage\`
3. Run **windeployqt** for Qt DLLs and plugins
4. Build **`dist\RadiaCodeMonitor-<version>-win64.exe`** via `installer\radiacode-monitor.nsi`  
   (`/DPRODUCT_VERSION=…` is always set by the script)

| Path | Role |
|------|------|
| `CMakeLists.txt` | **Source of version** (`project(... VERSION x.y.z)`) |
| `installer/radiacode-monitor.nsi` | NSIS script (version from packaging script) |
| `installer/license.txt` | License page in the wizard |
| `scripts/package-windows.ps1` | Stage + windeployqt + makensis |
| `dist/` | Output (gitignored) |

## macOS app package

After a **Release** build of the `.app` (Qt Creator kit or CLI):

```bash
# From the radiacode-monitor repo root
# Version is read automatically from CMakeLists.txt (project(... VERSION x.y.z))
./scripts/package-macos.sh

# Explicit paths / DMG (optional --version override):
./scripts/package-macos.sh \
  --bin-dir build/Qt_6_11_1_for_macOS_Release/bin \
  --qt-dir "$HOME/Qt/6.11.1/macos" \
  --dmg
```

This will:

1. Copy the built `radiacode-monitor.app` into `dist/stage/`
2. Run **macdeployqt** (Qt frameworks, plugins, and linked dylibs including QtRadiacode / libusb when found via `-libpath`)
3. Ad-hoc **codesign** so it runs outside the build tree
4. Publish **`dist/Radiacode Monitor.app`**
5. Optionally create **`dist/RadiacodeMonitor-<version>-macos.dmg`** (`--dmg`)

| Path | Role |
|------|------|
| `macos/Info.plist.in` | Bundle id, Bluetooth privacy strings, icon |
| `macos/radiacode-monitor.icns` | Dock / Finder icon |
| `scripts/package-macos.sh` | Stage + macdeployqt + codesign (+ optional DMG) |
| `dist/` | Output (gitignored) |

**Run locally:** `open "dist/Radiacode Monitor.app"` or drag to **Applications**.

**Distribution note:** ad-hoc signing is enough on *your* Mac. For other users, Gatekeeper expects an **Apple Developer ID** signature and **notarization** (not automated by this script).

## Linux Debian package (`.deb`)

Builds against **system Qt 6** (e.g. Ubuntu 24.04 = Qt 6.4) and packages the app, `libQtRadiacode.so`, desktop entry, icons, and USB **udev** rules.

**Build dependencies (Ubuntu 24.04):**

```bash
sudo apt install build-essential cmake ninja-build \
  qt6-base-dev qt6-connectivity-dev libusb-1.0-0-dev \
  dpkg-dev file
```

**Package** (repo root; sibling `../qtradiacode` recommended):

```bash
./scripts/package-deb.sh
# optional:
# ./scripts/package-deb.sh --clean
# ./scripts/package-deb.sh --build-dir build-deb --version 0.2.2
```

Output: **`dist/radiacode-monitor_<version>_<arch>.deb`**

**Install:**

```bash
sudo apt install ./dist/radiacode-monitor_*.deb
# unplug/replug the USB detector after install (postinst reloads udev)
```

| Path | Role |
|------|------|
| `scripts/package-deb.sh` | Release configure (`prefix=/usr`) + build + CPack DEB |
| `linux/debian/postinst` / `postrm` | Reload udev rules on install/remove |
| `linux/radiacode-monitor.desktop.in` | Menu entry |
| CMake `CPack` block | Package metadata + `dpkg-shlibdeps` |

Manual alternative from an existing build configured with `-DCMAKE_INSTALL_PREFIX=/usr`:

```bash
cd build-deb && cpack -G DEB
```

## GitHub / library pin

Private repos:

- App: https://github.com/petriska/radiacode-monitor
- Library: https://github.com/petriska/qtradiacode (**`v0.1.3`**)

Local development uses a **sibling** checkout (`../qtradiacode`). For a pinned clone:

```bash
git clone https://github.com/petriska/qtradiacode.git
cd qtradiacode && git checkout v0.1.3
```

Or CMake FetchContent (also used automatically if no local tree is found):

```cmake
FetchContent_Declare(qtradiacode
  GIT_REPOSITORY https://github.com/petriska/qtradiacode.git
  GIT_TAG        v0.1.3
)
```

## ROI time series

Universal spectrum multiscaling: define energy ROIs, sample on a dwell interval, export rates for offline analysis.

1. Connect; check energy calibration on the **Spectrum** tab.
2. Open **ROI time series**. Load a **preset** (radon daughters, Ag neutron activation, …) or **Add ROI** with name + keV range.
3. Optionally set **t₀** (flush / end of irradiation / any experiment marker).
4. **Start recording** (spectrum reset on start recommended).
5. **Stop** → **Export CSV…** → fit offline (e.g. \(\ln(\mathrm{cps})\) vs `elapsed_s`).

### Example: radon washout (²¹⁴Pb / ²¹⁴Bi)

1. Preset **Radon daughters** (windows ~295, ~352, ~609 keV).
2. Flush radon-rich air → **Set t₀ marker**.
3. Record ~1.5–2 h; export CSV.
4. Literature T½ (approx.): ²¹⁴Pb ~26.8 min, ²¹⁴Bi ~19.9 min (single-exp fit is approximate due to parent–daughter coupling).

### Example: silver neutron activation (¹⁰⁸Ag / ¹¹⁰Ag)

1. Preset **Ag neutron activation** — main short-lived lines:
   - **¹⁰⁸Ag** ~633 keV (t½ ≈ 2.37 min, from ¹⁰⁷Ag(n,γ))
   - **¹¹⁰Ag** ~658 keV (t½ ≈ 24.6 s, from ¹⁰⁹Ag(n,γ))
   - Optional (off by default): **¹¹⁰ᵐAg** ~885 / ~937 keV for longer runs
2. Irradiate Ag foil → start recording (reset spectrum on start recommended) → **Set t₀** at end of irradiation / start of count.
3. Note: 633 and 658 keV are close; on CsI they may partially overlap — tighten windows after a calibration spectrum if needed.

Rates are **incremental** ROI counts per second of **device live time** between successive spectra when live time increases; after a reset the first sample uses \(N/T_\mathrm{live}\).

## Spectrum export

Save spectrum as **CSV**, **TKA**, **ANSI N42.42**, or [**NPES-JSON**](https://github.com/OpenGammaProject/NPES-JSON) (NPESv2).

## Features (later)

- In-app T½ fit, background ROI
- Linux AppImage (optional portable build)

## Notes

- **BLE** is typically one central at a time. If Home Assistant or a phone already holds the BLE link, Refresh will not list that device and Connect over BLE will fail — use **USB** instead (works independently).
- Linux udev (USB): see `qtradiacode/platform/linux/99-radiacode.rules`.
- Linux BLE: Bluetooth adapter powered (`bluetoothctl power on`); Qt may log a harmless `CAP_NET_ADMIN` note during scan.

## Development

This application (and the companion [qtradiacode](https://github.com/petriska/qtradiacode) library) was built with substantial assistance from **[Grok](https://x.ai/)** (xAI) — coding, debugging, and docs — under human direction for goals, hardware checks, and review.

## License

- **This application:** [MIT](LICENSE)
- **Third-party libraries** shipped with binary packages (Qt, libusb, QtRadiacode, …): see **[THIRD_PARTY.md](THIRD_PARTY.md)**

Qt and libusb are used as **shared libraries** (LGPL-friendly packaging via `windeployqt` / `macdeployqt`). Protocol notes and attribution for the companion library live in [qtradiacode](https://github.com/petriska/qtradiacode).
