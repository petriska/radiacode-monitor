# Changelog

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

### Notes

- Prefer **USB** while Home Assistant holds BLE
- Library pin for this release: **qtradiacode `v0.1.1`**
