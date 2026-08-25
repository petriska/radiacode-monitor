# Spectrogram time zoom (long history) — Design

**Date:** 2026-08-25  
**App:** radiacode-monitor  
**Branch:** `feature/spectrogram-history`  
**Status:** approved to spec (brainstorm 2026-08-25)

## Goal

Analyze a loaded `.rcsg` / `.rcsg.gz` history that is longer than the pane height at 1:1 (typically a day, up to ~48 h in RAM). The operator must **see the whole recording**, **zoom in to 1:1**, and **select a time × energy region longer than the 1:1 viewport** so Spectrum / MCS extract can cover hours (e.g. a 24 h integrated spectrum).

This slice is **in-widget time zoom** plus **raising the load cap**. It is not a week/month file-backed viewer.

## User story

Without a device (or with one): File → Load spectrogram history → the pane shows the **entire** buffer (Fit all). Ctrl+wheel zooms toward 1:1 around the cursor; wheel without Ctrl pans time. Drag a rectangle over a long span → Spectrum from selection / MCS from selection as today, but the box can cover the whole file.

## Constraints (locked)

| Rule | Decision |
|------|----------|
| Max zoom-in | **1 row = 1 pixel**. Rows never stretch taller than 1 px. |
| Zoom-out | `rowsPerPixel ≥ 1`; one screen pixel is a bin of consecutive buffer rows. |
| Interaction | **Ctrl+wheel** continuous zoom around the row under the cursor; unmodified wheel still pans. **Fit all** button (one-shot, not a lock). No discrete 15 min / 1 h / 24 h presets. |
| Downsample | **Max** of `displayRate` per channel in the bin (hotspots survive). Colour scale reference is unchanged (max over history × floor/ceil fractions). |
| Load RAM cap | **48 h at 1 s/row = 172 800 rows** (~1.4 GB at 1024 channels). Week/month is a later file-backed slice. |
| Live RAM | Unchanged: History combo 15 min … 4 h (max 8 h). 48 h is **load**, not live preset. |
| Colour max | **Incremental** on append (new row vs current max). No full-matrix scan per poll. |

## Non-goals

- File-backed / mmap / open a week of daily `.rcsg.gz` as one timeline  
- Time-stretch zoom-in (row taller than 1 px)  
- Separate overview minimap pane  
- Discrete zoom presets in the setup panel  
- Async/background load thread (wait cursor on the existing sync load is enough)  
- Downsampling MCS extract (24 h MCS stays one point per buffer row)  
- Changing `.rcsg` on-disk format  

---

## 1. Time model

`SpectrumWaterfall` stores `float m_rowsPerPixel` (default `1.0f`).

**Clamp**

- Minimum: `1.0`  
- Maximum: `max(1, nRows / plotHeight)` so Fit all is the most zoomed-out state. If `nRows ≤ plotHeight`, zoom is a no-op (already 1:1, bottom-aligned).

**`TimeView`**

- `rpp == 1`: identical to today — `visible = min(nRows, plotH)`, dest **bottom-aligned**, 1 px = 1 row.  
- `rpp > 1`: dest **fills** `imgRect`; each dest pixel maps to buffer rows  
  `[floor(firstRow + y * rpp), floor(firstRow + (y+1) * rpp))`.

`scrollFromNewest` stays in **buffer rows**. Visible buffer span ≈ `destHeight * rpp`.

**Y → row** (cursor, selection corners):

```
row = firstRow + floor((y - dest.top()) * rowsPerPixel)
```

**Row → Y** (selection rectangle, time ticks):

```
y = dest.top() + (row - firstRow) / rowsPerPixel
```

At `rpp == 1` both collapse to the current 1:1 mapping.

**Ctrl+wheel** (plot, not colour bar)

- Wheel up → zoom in (`rpp /= 1.25`, clamp to 1)  
- Wheel down → zoom out (`rpp *= 1.25`, clamp to max)  
- Keep the **buffer row under the cursor** at the same widget Y by recomputing `scrollFromNewest`.  
- Colour bar: Ctrl does not change this — wheel there still scales colour (today).

**Fit all** (one-shot)

- `rpp = max(1, nRows / plotHeight)`  
- `scrollFromNewest = 0` (whole buffer visible; newest at bottom)  
- Does **not** re-apply when live rows arrive. User clicks Fit all again if they want.

**Zoom 1:1** (context menu)

- `rpp = 1`  
- Recompute scroll so the **centre row of the current view** stays centred (clamp).  

**Follow live** (`scrollFromNewest == 0`) unchanged: newest at bottom. Zoom-out + follow live shows more history above the live edge. Double-click → follow live, unchanged.

---

## 2. Painting and selection

**Viewport cache** stays plot-sized (not a 48 h image).

**1:1 live follow:** keep today’s incremental `memmove` + new bottom scanline.

**`rpp > 1` live follow:** do **not** max-pool the whole buffer every poll. Recompute **only the bottom image row** from its bin (`O(rpp × channels)`). If `rpp` / scroll / size / colour-max changed, full `rebuildViewportImage`.

**Fit all / scroll / zoom:** `rebuildViewportImage` max-pools visible bins only. A one-shot Fit all over 48 h is `O(nRows × channels)` once — acceptable. Subsequent live updates must not repeat that full pass.

**Selection** stays in buffer coordinates `(ch0, ch1, row0, row1)` as today. Extract Spectrum / MCS **unchanged** (sum / per-row over the selected rows). A 24 h box is a large `row1 - row0`; the MCS dialog may have ~10⁴–10⁵ points — keep all points.

**Move / resize:** pixel drag → row delta via `round(dY * rowsPerPixel)` so the box tracks the pointer when zoomed out.

**Hit testing:** keep ~7 px edge slop. A one-bin selection at strong zoom-out may be 1 px tall.

Minimum time selection at a given zoom = one bin; finer span = zoom in and redraw.

**Colour:** zoom-out pixels look hotter (max of N seconds). Scale still uses `m_displayMax` over the **full** history, not the viewport.

---

## 3. Load capacity

Raise `kMaxRowsCap` to **172 800** (48 × 3600). Live `kMaxHistoryMinutes` stays 8 h; live `recomputeCapacity()` still derives rows from the History combo and is far below the new cap.

**`loadHistory` must not trim using `historyMinutes` from the file.** Today that drops a long `.rcsg` to 15 min–8 h.

After a successful load:

1. Replace `m_rows` with the file (or the **newest** 172 800 rows if the file is larger).  
2. Set `m_historyCapUnlocked = true` and `m_maxRows = kMaxRowsCap` so later live appends can grow toward 48 h without immediately dropping the file.  
3. If the file was truncated: `QMessageBox` with loaded vs total row counts.  
4. Wait cursor for the duration of load/gunzip.  
5. **Fit all** + follow live (newest at bottom, whole buffer on screen).  
6. One colour-max pass while loading (see §5).

`recomputeCapacity()` (History combo **or** integrate change) must **not** trim a loaded long buffer while `m_historyCapUnlocked` is set — otherwise changing integrate from 1 → 2 would drop a 24 h load down to the 15 min–8 h live formula.

Clear `m_historyCapUnlocked` only when the user **changes the History combo**. That path may trim the ring (intentional). Reset / new load / channel mismatch replace the buffer as today.

---

## 4. Live, acquisition, disconnect

Loaded history and live polls share one ring. No extra “review mode”.

| Event | Waterfall |
|-------|-----------|
| Load while disconnected | Buffer = file. Fit all. |
| Connect / polls after load | First spectrum = new baseline (no row). Later polls **append** after the file. Wall-clock gap is **not** drawn as empty hours. |
| Channel count mismatch | Wipe rows (cannot merge). |
| **Disconnect** | **Do not** `waterfall->clear()`. Stop recorder; clear spectrum plot / device labels as today. Analysis of the loaded (and any appended) history continues. |
| **Reset spectrum** (button) | Still **clears** waterfall (explicit). |
| Acquisition **Continue…** | Waterfall keeps appending. |
| Acquisition **Reset and start** | Device `spectrumReset` only. Waterfall **kept**; `makeRow` already resyncs baseline when live time does not advance. |
| Acquisition complete / Stop | Spectrum plot freezes; waterfall **keeps** taking live rates. |
| Record continuously | Only **new** live rows go to today’s `.rcsg`. Loaded RAM is not rewritten to disk. |

Zoom does not auto-refit while measuring. Follow live vs scrolled-into-history behaves as today (`scrollFromNewest` holds the window when not following).

---

## 5. Colour max (performance)

Today `matrixMaxRate()` walks every channel of every row on **each** `appendDisplayRow`. At 48 h that is ~177 M comparisons per second — not acceptable.

There is **no** matrix min scan. Colour floor is `displayMax * floorFraction`.

**Per-row peak:** each `Row` stores `float peak` = max `displayRate` on that row in the **current** Live/Net mode (~0.7 MB extra at cap). Do not keep a second live-only peak; on Live ↔ Net (or BG change) recompute every `peak` in one pass.

| Event | Action |
|-------|--------|
| Append | `peak = max(displayRate)` on the new row. If `peak > displayMax * (1 + 2% hysteresis)` → `displayMax = peak`, recolour. **No** full-matrix walk. |
| Drop oldest | If `dropped.peak` is not within hysteresis of `displayMax`, leave scale. Else `displayMax = max(remaining row peaks)` — `O(nRows)`, not `O(nRows × channels)`. |
| Load | Compute `peak` per row while filling; `displayMax` from those peaks (one pass). |
| Live ↔ Net, BG set/clear, colour-scale reset | Recompute all row peaks + `displayMax` once (rare). |

Full recolour of the viewport still happens only when `displayMax` moves (existing hysteresis). Incremental live paint at 1:1 is unchanged when the scale is stable.

---

## 6. UI

| Control | Where |
|---------|--------|
| **Fit all** | Button next to Follow live; context menu |
| **Zoom 1:1** | Context menu |
| Ctrl+wheel | Plot only (documented in widget tooltip) |
| Badge | Draw in the existing waterfall overlay (next to Live/Net / hist). Text: `1:1` when `rpp == 1`, else visible span (e.g. `~6 h`) from first/last **visible** row `wallTime` (fallback: sum of `intervalSec`) — not raw `rowsPerPixel`. |

No new setup-panel zoom combo. Tooltip: add Ctrl+wheel zoom, Fit all, and “max zoom 1:1”. Left-side time labels keep working via the new Y mapping.

`rowsPerPixel` is **not** persisted in `QSettings` (session default 1:1; load applies Fit all).

---

## Error handling

- Load / gunzip failures: existing warning dialogs.  
- Empty file: existing warning.  
- File longer than cap: load newest 172 800 rows + warning (`Loaded N of M rows (limit 48 h at 1 s/row)`).  
- Fit all / Ctrl+wheel with empty buffer: no-op.  
- MCS / spectrum extract on a huge selection: same code path; no extra error unless existing export fails.

---

## Files

| Path | Change |
|------|--------|
| `src/spectrumwaterfall.h/.cpp` | `rowsPerPixel`, `TimeView`, paint bins, selection mapping, incremental colour max, `kMaxRowsCap`, load trim, Fit all / Zoom 1:1, Ctrl+wheel |
| `src/mainwindow.cpp` | Fit all button; disconnect **without** `waterfall->clear()`; optional badge label if not painted inside the widget |
| `CHANGELOG.md` | Unreleased notes |
| `AGENTS.md` | Time-zoom + 48 h load cap; drop stale “still planned” items that this branch already shipped |

---

## Testing (manual, no device required)

Use local `.rcsg` under `spectrograms/` (untracked).

1. Load a short file (~thousands of rows): auto Fit all; Ctrl+wheel in to 1:1 (bottom-aligned if rows < pane); wheel pans; Zoom 1:1 and Fit all from the menu.  
2. Select a box **taller in time than the old 1:1 viewport** while zoomed out → Spectrum from selection duration ≈ selected wall span; MCS point count ≈ selected row count.  
3. Zoom in, move/resize the box — it stays on the same buffer rows.  
4. If a ≥ several-hour file exists: Fit all shows the full span in the badge; load does not drop to 2 h.  
5. Colour bar / Net / BG still work; bringing a hot bin in should raise scale without a per-poll hitch.  
6. **Disconnect must not** wipe a loaded waterfall (connect a device when available). Reset spectrum **does** wipe.  
7. With a device: load then let polls run — rows append; Fit all scale stays until clicked again; Record continuously writes only new rows.

## Later (not this slice)

- File-backed week/month (folder of daily `.rcsg.gz`, viewport + pyramid).  
- ROI from waterfall selection.  
- Seamless disk resume of RAM from today’s file on startup.  
