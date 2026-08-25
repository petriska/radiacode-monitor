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

- Time axis **maximum zoom is 1:1** (one history row = one screen pixel), bottom-aligned
  (SDR-style scroll). Zoom-out uses `rowsPerPixel` max-pool so hotspots stay visible;
  do **not** stretch rows above 1 px (that remapped history).
- Colour scale = 0 … max rate over history; full recolour only when that max changes (~2% hysteresis).
- **History buffer** (feature branch / post-0.3.0): minutes preset (15–240, default 120),
  wheel scroll, follow-live, per-row `wallTime`. Viewport-sized image cache only.
  Ctrl+wheel time zoom; Fit all / Zoom 1:1.

**Spectrogram I/O (feature branch)**

- `.rcsg` binary via `src/spectrogramfile.*` — see header for layout; `AppendWriter` for continuous.
- `SpectrogramRecorder` — daily `.rcsg` append; day roll → `.rcsg.gz` + keepDays prune.
- `SpectrogramCompress` — gzip via system zlib (Ubuntu: `zlib1g-dev`) or Qt-bundled `QtZlib`.
- Save/load full buffer; continuous checkbox in Live panel.
- Load cap **172 800** rows (48 h at 1 s/row); `m_historyCapUnlocked` after file load
  (no trim to History combo; combo is disabled until Reset spectrum). Incremental
  colour max via per-row `Row::peak`. Disconnect invalidates the live ΔN baseline
  without `waterfall->clear()`. Zoomed-out live follow last-line-only when
  `firstRow` is stable; full viewport rebuild when `firstRow` advances.

**Still planned (“spectrogram analysis”)**

- ROI definition from waterfall selection
- File-backed week/month timeline
- Seamless RAM resume from today’s `.rcsg` on startup

Work spectrogram features on `feature/spectrogram-history` (or successor) from `main`.

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
