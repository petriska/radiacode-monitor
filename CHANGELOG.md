# Changelog

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
