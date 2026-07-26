# radiacode-monitor

Simple **Qt 6 Widgets** desktop app for [RadiaCode](https://www.radiacode.com/) detectors using the **QtRadiacode** library.

## Features (MVP)

- List openable USB devices
- Connect / disconnect by serial
- Live count rate and dose rate (1 s poll)
- Spectrum plot (1024 channels)
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

## Later (GitHub)

Pin the library via FetchContent or git submodule to private `qtradiacode` `v0.1.0`.

## Features (later)

- Spectrum live time, total counts
- Save spectrum: CSV, TKA, ANSI N42.42, [NPES-JSON](https://github.com/OpenGammaProject/NPES-JSON)
- Auto-refresh spectrum (~2 s)

## Notes

- Prefer **USB** while Home Assistant holds BLE.
- Linux udev: see `qtradiacode/platform/linux/99-radiacode.rules`.

## Development

This application (and the companion [qtradiacode](https://github.com/petriska/qtradiacode) library) was built with substantial assistance from **[Grok](https://x.ai/)** (xAI) — coding, debugging, and docs — under human direction for goals, hardware checks, and review.

## License

MIT (same spirit as QtRadiacode). See the library repository for protocol attribution.
