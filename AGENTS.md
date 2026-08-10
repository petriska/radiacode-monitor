# radiacode-monitor — agent / contributor notes

Desktop **Qt 6 Widgets** app for [RadiaCode](https://www.radiacode.com/) detectors.
Unofficial community software. Companion library: **[qtradiacode](https://github.com/petriska/qtradiacode)**.

This file is for AI assistants (Grok, Claude Code, Cursor, …) and humans after a fresh clone
on any machine (Windows, macOS, Linux). Prefer it over guessing project conventions.

---

## Repos and layout

Recommended sibling checkouts:

```text
workspace/
  qtradiacode/            # library (USB + BLE, protocol)
  radiacode-monitor/      # this app
```

- App version: `project(... VERSION …)` in root `CMakeLists.txt` (source of truth).
- Library pin for FetchContent: `QTRADIACODE_GIT_TAG` (default **`v0.1.3`**).
- Public app repo: `https://github.com/petriska/radiacode-monitor`
- Changelog: `CHANGELOG.md` — keep user-facing notes for releases.

---

## Stack

| Piece | Notes |
|-------|--------|
| Language | C++17 |
| UI | Qt 6 **Widgets** only (not QML) |
| Build | CMake ≥ 3.21, `AUTOMOC` / `AUTORCC` |
| Device I/O | via **QtRadiacode** (`RadiaCodeDevice`, discovery USB+BLE) |
| Platforms | Windows (MSVC), macOS, Linux (e.g. Ubuntu 24.04 / Qt 6.4+) |

No unit-test suite required by default; verify with a Release build and manual device smoke when possible.

---

## Build (any host)

```bash
# From radiacode-monitor/
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x
cmake --build . --config Release   # multi-config generators need --config
```

- Local library: auto-detected as `../qtradiacode` or set `-DQTRADIACODE_DIR=...`.
- No local library: CMake **FetchContent** clones `qtradiacode` at `QTRADIACODE_GIT_TAG`.
- Windows (Qt Creator / JOM style): often need VS `vcvars64` + `jom` on PATH for NMake/JOM builds.
- Packaging: `scripts/package-windows.ps1`, `scripts/package-macos.sh`, `scripts/package-deb.sh`.

Do **not** commit `build/`, `dist/stage/`, or machine-local CMake caches.

---

## Source map

| Path | Role |
|------|------|
| `src/main.cpp` | Entry, app icon |
| `src/mainwindow.*` | Main UI, device connect, poll, wires spectrum / waterfall / ROI / acquisition |
| `src/acquisition/` | Timed / count-based acquisition state machine |
| `src/spectrumwidget.*` | Live spectrum plot (zoom/pan, cursor, ROI tint) |
| `src/spectrumwaterfall.*` | Live waterfall (ΔN/Δt rates, Live/Net, integrate) |
| `src/spectrumexport.*` | Save spectrum CSV / TKA / N42 / NPES-JSON |
| `src/roitimeseries/` | ROI table, dwell recording, time-series chart, CSV export |
| `docs/superpowers/specs/` | Design notes for slices (when present) |

Poll loop lives in `MainWindow` (~1 s USB; BLE cadence differs). Spectrum pushes feed waterfall and ROI recorder.

---

## Product status (as of 0.3.0)

**In tree / released**

- USB + BLE connect, live CPS / dose / battery / RSSI
- Spectrum plot, multi-format save
- ROI time series tab (presets, custom ROIs, CSV)
- Acquisition run (live time or total counts; Continue/Save/Reset dialog)
- Load background spectrum → Live / Background / Net views
- Waterfall under spectrum: rates, integrate N polls, Live/Net, cross-linked cursor
- Compact UI (no separate log panel; status bar)

**Waterfall behaviour (important)**

- Time axis is **1:1**: one history row = one screen pixel, bottom-aligned (SDR-style scroll).
- Do **not** stretch the full row buffer to fill the pane height (that made history look “remapped”).
- Colour scale = 0 … max rate over history; full recolour only when that max changes (~2% hysteresis).
- Current history depth is a **short live buffer** (~`maxRows`, not multi-hour).

**Explicitly not in 0.3.0 — planned later (“spectrogram analysis”)**

- Long ring buffer (1–2+ h), scroll-back through history
- Continuous on-disk spectrogram + reload/continue after restart (needs per-row timestamps)
- Larger waterfall as primary analysis surface
- Rectangle select on waterfall → spectrum from X, MCS/time series from Y
- ROI definition from waterfall selection
- Offline session open/export of spectrogram

Prefer a **new feature branch** from `main` / tag `v0.3.0` for that work; do not silently reinvent half of it inside unrelated fixes.

---

## Conventions

### Code

- Match existing style in the file you edit (Qt types, `tr()`, signal/slot lambdas in `MainWindow`).
- Keep UI strings user-facing via `tr()`.
- Prefer small, focused diffs; no drive-by refactors or unsolicited markdown docs.
- New sources: add to `CMakeLists.txt` `_radiacode_monitor_sources`.

### Git / release

- Commits: short imperative subject; optional body for why.
- Release style: bump `CMakeLists.txt` version, finalize `CHANGELOG.md` (`[Unreleased]` → version section), tag `vX.Y.Z`, GitHub release.
- User often works from several machines; **commit and push** durable decisions — do not rely on local-only agent memory.

### Safety

- No secrets in repo. Device serials in UI are fine; do not hardcode personal paths.
- Do not force-push `main` or rewrite published release tags without explicit human request.

---

## Quick orientation for a new session

1. Read `CHANGELOG.md` top section and `CMakeLists.txt` version.
2. `git status` / current branch; prefer branching from up-to-date `main`.
3. Build once before claiming fixes.
4. For waterfall/spectrum work: read `spectrumwaterfall.h` comments and the 1:1 / colour-scale rules above.
5. Device hardware may be unavailable on some hosts (e.g. remote GPU box) — still keep code correct; note when only compile-verified.

---

## Related docs

- `README.md` — user-facing build and packaging
- `CHANGELOG.md` — release history
- `THIRD_PARTY.md` / `LICENSE` — licensing
- `docs/superpowers/specs/` — slice design docs when present
