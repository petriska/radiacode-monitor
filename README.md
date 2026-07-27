# radiacode-monitor

Simple **Qt 6 Widgets** desktop app for [RadiaCode](https://www.radiacode.com/) detectors using the **QtRadiacode** library.

## Features (MVP)

- List openable USB devices
- Connect / disconnect by serial
- Live count rate and dose rate (1 s poll)
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

```bash
# Expected layout:
#   workspace/qtradiacode/       # library
#   workspace/radiacode-monitor/ # this app

cd radiacode-monitor
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x
# or: cmake .. -DQTRADIACODE_DIR=/path/to/qtradiacode
cmake --build .
./radiacode-monitor
```

## Windows installer (NSIS)

After a **Release** build (shared `QtRadiacode.dll` + `libusb-1.0.dll` next to the exe):

**Requirements:** [NSIS 3.x](https://nsis.sourceforge.io/), Qt kit with `windeployqt` / `windeployqt6`.

```powershell
# From the radiacode-monitor repo root
.\scripts\package-windows.ps1

# Or with explicit paths / version:
.\scripts\package-windows.ps1 `
  -BuildBinDir .\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Release\bin `
  -QtDir M:\Qt\6.11.1\msvc2022_64 `
  -Version 0.1.0
```

This will:

1. Stage files under `dist\stage\`
2. Run **windeployqt** for Qt DLLs and plugins
3. Build **`dist\RadiaCodeMonitor-<version>-win64.exe`** via `installer\radiacode-monitor.nsi`

| Path | Role |
|------|------|
| `installer/radiacode-monitor.nsi` | NSIS script |
| `installer/license.txt` | License page in the wizard |
| `scripts/package-windows.ps1` | Stage + windeployqt + makensis |
| `dist/` | Output (gitignored) |

## Later (GitHub)

Pin the library via FetchContent or git submodule to private `qtradiacode` `v0.1.0`.

## ROI time series

Universal spectrum multiscaling: define energy ROIs, sample on a dwell interval, export rates for offline analysis.

1. Connect; check energy calibration on the **Spectrum** tab.
2. Open **ROI time series**. Load a **preset** (e.g. radon daughters) or **Add ROI** with name + keV range.
3. Optionally set **t₀** (flush / end of irradiation / any experiment marker).
4. **Start recording** (spectrum reset on start recommended).
5. **Stop** → **Export CSV…** → fit offline (e.g. \(\ln(\mathrm{cps})\) vs `elapsed_s`).

### Example: radon washout (²¹⁴Pb / ²¹⁴Bi)

1. Preset **Radon daughters** (windows ~295, ~352, ~609 keV).
2. Flush radon-rich air → **Set t₀ marker**.
3. Record ~1.5–2 h; export CSV.
4. Literature T½ (approx.): ²¹⁴Pb ~26.8 min, ²¹⁴Bi ~19.9 min (single-exp fit is approximate due to parent–daughter coupling).

Rates are **incremental** ROI counts per second of **device live time** between successive spectra when live time increases; after a reset the first sample uses \(N/T_\mathrm{live}\).

## Features (later)

- In-app T½ fit, ROI bands on spectrum, background ROI
- Save spectrum: CSV, TKA, ANSI N42.42, [NPES-JSON](https://github.com/OpenGammaProject/NPES-JSON) (already supported)

## Notes

- Prefer **USB** while Home Assistant holds BLE.
- Linux udev: see `qtradiacode/platform/linux/99-radiacode.rules`.

## Development

This application (and the companion [qtradiacode](https://github.com/petriska/qtradiacode) library) was built with substantial assistance from **[Grok](https://x.ai/)** (xAI) — coding, debugging, and docs — under human direction for goals, hardware checks, and review.

## License

MIT (same spirit as QtRadiacode). See the library repository for protocol attribution.
