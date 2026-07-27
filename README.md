# radiacode-monitor

Simple **Qt 6 Widgets** desktop app for [RadiaCode](https://www.radiacode.com/) detectors using the **QtRadiacode** library.

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

## GitHub / library pin

Private repos:

- App: https://github.com/petriska/radiacode-monitor
- Library: https://github.com/petriska/qtradiacode (**`v0.1.1`**)

Local development uses a **sibling** checkout (`../qtradiacode`). For a pinned clone:

```bash
git clone git@github.com:petriska/qtradiacode.git
cd qtradiacode && git checkout v0.1.1
```

Or CMake FetchContent:

```cmake
FetchContent_Declare(qtradiacode
  GIT_REPOSITORY git@github.com:petriska/qtradiacode.git
  GIT_TAG        v0.1.1
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
- Linux AppImage / deb packaging

## Notes

- **BLE** is typically one central at a time. If Home Assistant or a phone already holds the BLE link, Refresh will not list that device and Connect over BLE will fail — use **USB** instead (works independently).
- Linux udev (USB): see `qtradiacode/platform/linux/99-radiacode.rules`.
- Linux BLE: Bluetooth adapter powered (`bluetoothctl power on`); Qt may log a harmless `CAP_NET_ADMIN` note during scan.

## Development

This application (and the companion [qtradiacode](https://github.com/petriska/qtradiacode) library) was built with substantial assistance from **[Grok](https://x.ai/)** (xAI) — coding, debugging, and docs — under human direction for goals, hardware checks, and review.

## License

MIT (same spirit as QtRadiacode). See the library repository for protocol attribution.
