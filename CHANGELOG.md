# Changelog

## [Unreleased]

### Added

- **Acquisition run** (Live panel): stop by **device live time (s)** or **total
  spectrum counts**, Start/Stop, horizontal progress bar + detail text.
  Stop is evaluated after each full spectrum (± one poll frame).
- Acquisition start when spectrum has data: dialog **Continue…** (keep
  accumulation) / **Save…** / **Reset and start** / **Cancel** — no separate
  reset checkbox.
- **Background / Net spectrum**: **Load BG…** opens file dialog (CSV/TKA/N42/NPES-JSON);
  view combo Live / Background / Net enabled only after BG is loaded
  (grayed out otherwise). Workflow: Save spectrum → Load BG → Net.
  Waterfall follows **Live** / **Net** view (rebuilds rates from stored snapshots).
- Waterfall **integrate**: each display row covers N spectrum polls (1…32);
  longer history (~N×), coarser time; control under View, saved in QSettings.
- **Spectrum waterfall** (under spectrum plot): time × channel count-rate
  (ΔN/Δt between snapshots), SDR-style colors, newest row at bottom; X range
  follows spectrum zoom/pan. Hover cursor: energy/channel, cps, ΔN, device
  live time, age from newest (`t−Ns`), wall clock. Spectrum ↔ waterfall
  cursors are cross-linked (same channel highlighted on both).
- **UI layout** (1080p-friendly): Device bar moved next to Live (above ROI
  controls); Live form drops Serial/Firmware/Dose rate (serial/FW on Device
  row; dose still updated internally); Messages/log panel removed (status bar only).

## [0.2.2] — 2026-07-29

### Added

- **Debian package** via CPack: `scripts/package-deb.sh` →
  `dist/radiacode-monitor_<ver>_<arch>.deb` (app + `libQtRadiacode`,
  desktop/icons, udev rules, docs; `postinst` reloads udev + desktop/icon caches)
- Desktop entry polish: `StartupWMClass`, Science category (Ubuntu app menu + icon)

### Fixed

- Build against distro **Qt 6.4** (Ubuntu 24.04 LTS): requires **qtradiacode ≥ 0.1.3**
  (`QPermissions` / `QT_CONFIG(permissions)` guard). macOS with Qt 6.5+ (e.g. 6.11.1)
  is unaffected — Bluetooth permission plugin + Info.plist strings remain as before.

### Notes

- Library pin for this release: **qtradiacode `v0.1.3`**
- Linux deb targets Ubuntu 24.04 **amd64** (system Qt 6); install from `/tmp` avoids apt `_apt` notice

## [0.2.1] — 2026-07-28

### Added

- Menu bar: **File** (Save Spectrum, Export ROI CSV, Exit) and **Help** (About, About Qt)
- Root **LICENSE** (MIT), **THIRD_PARTY.md**, unofficial disclaimer in README / About
- About dialog: app version, Qt runtime/build, license links, source/releases URLs
- Transparent app icons (PNG/ICO); `scripts/make-icon-transparent.py`
- CMake: app version from `project(... VERSION)` (`RADIACODE_MONITOR_VERSION`)
- Packaging: auto version from CMakeLists for Windows/macOS scripts; ship LICENSE + THIRD_PARTY in Windows stage
- CMake: find local `qtradiacode` sibling **or** `qtradiacode-*` release ZIP folders, else **FetchContent** pin `QTRADIACODE_GIT_TAG` (default `v0.1.2`)

### Notes

- Library pin for this release: **qtradiacode `v0.1.2`**
- Build-from-source ZIP documented in README

## [0.2.0] — 2026-07-28

### Added

- Device list includes **BLE** scan results alongside USB; Connect uses `connectBle` / `connectUsb`
- Live: **Battery** (% from Rare DATA_BUF) and **BLE signal** (RSSI dBm; n/a on USB)
- Periodic DATA_BUF poll for Rare/battery (~1 min + shortly after connect)
- Live: **Spectrum total counts** (sum of channels on the current spectrum)
- ROI table columns **Counts** (cumulative in energy window) and **cps** (ΔN/Δt, same as chart while recording)
- ROI preset **Ag neutron activation** (¹⁰⁸Ag 633 keV, ¹¹⁰Ag 658 keV; optional ¹¹⁰ᵐAg 885/937 keV)
- QSettings: remember last ROI **preset**, **dwell**, and **Reset spectrum on start**
- **macOS** packaging (`scripts/package-macos.sh`, app icon, Info.plist); Linux/Windows icons

### Changed

- UI group renamed to **Device** (was “USB device”)
- ROI controls (preset, table, record) moved next to **Live** (always visible); ROI tab shows chart only
- **Reset spectrum** / **Save spectrum…** moved from Device bar into the **Live** panel (under Spectrum live time)
- Live and ROI time series group boxes share the same row height

### Fixed

- BLE: one command per second + spectrum gating (no queue full spam)
- BLE ROI: dwell uses wall-clock time (was inflated to ~3× by round-robin)
- ROI table: click outside clears the highlighted row selection

### Notes

- Prefer **USB** while Home Assistant holds BLE
- Library pin for this release: **qtradiacode `v0.1.1`** (later builds use newer tags; see 0.2.1)

## [0.1.0] — 2026-07-27

### Added

- Qt 6 Widgets desktop monitor for RadiaCode via **QtRadiacode**
- USB device list, connect/disconnect by serial
- Live count rate and dose rate (1 s poll)
- Interactive spectrum plot (1024 channels / keV, √ Y-scale)
  - Wheel zoom, drag pan, double-click reset, energy cursor
  - ROI color tint on spectrum bars (overlap blend)
- Spectrum auto-refresh; live time and total counts
- Spectrum export: CSV, TKA, ANSI N42.42, OpenGamma NPES-JSON (NPESv2)
- Reset spectrum; temperature / charge from DATA_BUF when present
- **ROI time series** tab: presets (radon ²¹⁴Pb/²¹⁴Bi), custom energy windows,
  dwell sampling, t₀ marker, multi-curve chart, CSV export
- Windows GUI subsystem (no console) and **NSIS** packaging
  (`scripts/package-windows.ps1`, `installer/radiacode-monitor.nsi`)
- **QSettings**: remember last spectrum export format (and directory) after a successful save

### Notes

- Prefer **USB** while Home Assistant holds BLE
- Library pin for this release: **qtradiacode `v0.1.1`**
