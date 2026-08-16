# Spectrogram colour-bar window (SDR Console style) — Design

**Date:** 2026-08-16  
**App:** radiacode-monitor  
**Branch:** `feature/spectrogram-history`  
**Status:** approved to implement (brainstorm)

## Goal

The colour bar to the right of the waterfall shows the same window that already maps rates to colours: the palette is **compressed** between floor and ceil. Below the window the bar is black; above it, white. Dragging the middle of the coloured band slides the whole window.

This is a visual + hit-test change. Rate mapping (`rateToColor`) already uses `[floor, ceil] × autoMax`.

## User story

Operator wants a weak band (e.g. 20–50 % of auto-max) to use the full palette. They pinch the bar to that slice: 0–20 % black, 20–50 % compressed scale, 50–100 % white. Grabbing the middle of the coloured band slides the same-width window (20–50 → 40–70). Double-click / **Reset colour scale** returns to 0–100 %.

## Current vs wanted

| | Today | Wanted |
|---|---|---|
| Bar paint | Full palette over the whole bar | Black / compressed palette / white |
| Handles | Two sliding tick lines (cream + blue) | Edges of the coloured band (+ thin marks) |
| Bar axis | 0 … `kColorCeilMax` (200 % of auto-max) | **0 … auto-max (100 %)** |
| Click | Top 55 % = ceil, bottom = floor | Hit-test: edge / interior / outside |
| Mid-drag | — | Slides floor and ceil together |
| Spectrogram colours | Already windowed | Unchanged |

Default (floor 0, ceil 1.0): the bar is a full rainbow, bottom cold, top hot — no black/white pads.

## Mapping (unchanged)

```
lo = autoMax × floorFrac
hi = autoMax × ceilFrac
t  = clamp((rate − lo) / (hi − lo), 0, 1)   // then existing gamma 0.55 + palette
```

- `rate < lo` → black (palette start)  
- `rate > hi` → white (palette end)  
- `autoMax` = max rate over history, same ~2 % hysteresis as today  

`colorMapMin` / `colorMapMax` stay as they are.

## Bar geometry

- Same place: `plot.right() + kColorBarGap`, width `kColorBarWidth`, height = plot.  
- **Y axis of the bar is 0 at the bottom and 1.0 (auto-max) at the top.**  
- Floor Y = lerp(bottom, top, floorFrac) when `floorFrac` ∈ [0, 1].  
- Ceil Y = lerp(bottom, top, min(ceilFrac, 1.0)).  
- If `ceilFrac > 1.0` (spinbox up to 200 %): coloured band goes to the **top** of the bar; no white pad. Spectrogram still uses the real `ceilFrac` (less sensitive).  
- If `ceilFrac < 1.0` and `floorFrac == 0`: colour from bottom to ceil Y, white above.  
- Thin 1–2 px marks at the two edges so grab targets stay visible.

Paint order: black `[bar.bottom … floorY]`, palette `[floorY … ceilY]` (same gamma as `rateToColor`), white `[ceilY … bar.top]`, then border + edge marks.

## Interaction

| Hit | Action | Cursor |
|-----|--------|--------|
| Within ~6 px of ceil edge | Drag **ceil** only | `SizeVer` |
| Within ~6 px of floor edge | Drag **floor** only | `SizeVer` |
| Interior of coloured band | Drag **window** (both fracs + same Δ) | `SizeAll` |
| Black or white pad | Jump **nearest** edge to cursor, then drag that edge | `SizeVer` |

Edge hits win over interior when both apply (narrow window).

**Window drag:** `Δ = newFrac − pressFrac` at the press Y; add `Δ` to both floor and ceil; clamp so the window stays inside `[0, max(1.0, ceilFrac)]` without changing width. If the current width cannot fit (e.g. ceil was 1.6 and floor 0.9), clamp against 0 and the current ceil max without shrinking.

**Min gap:** floor stays strictly below ceil (keep today’s ~5 % of ceil, or a small absolute epsilon). Floor is **not** capped at `kColorFloorMax` (0.5) anymore — a window must be allowed to sit at 60–90 %.

**Spinbox Colour scale %:** still sets **ceil** only (5–200 %), persisted as `waterfall/colorScalePercent`. Signal `colorScaleChanged` unchanged.

**Reset:** double-click on the bar and context **Reset colour scale** → floor 0, ceil 1.0.

**Wheel** on the bar: no new behaviour (wheel on the plot still pans history).

## Fractions

| Symbol | Range after this change | Notes |
|--------|-------------------------|--------|
| `floorFrac` | `[0, ceilFrac − minGap]` | Drop the 0.5 hard cap |
| `ceilFrac` | `[kColorCeilMin, kColorCeilMax]` (0.02…2.0) | Unchanged |

`setColorFloorFraction` / `setColorCeilFraction` stay the public API; only clamps and bar Y mapping change.

## Non-goals

- New widgets or a dual-range slider in the Live panel  
- Changing the palette or gamma  
- Colour-bar decoration on PNG export (PNG is rasterized from rates, not a widget shot)  
- Wheel-to-zoom the colour window  
- Numeric labels on the bar (cps ticks)

## Files

| Path | Role |
|------|------|
| `src/spectrumwaterfall.h` | Drag mode enum (floor / ceil / window); drop or raise `kColorFloorMax`; hit-test helper |
| `src/spectrumwaterfall.cpp` | `drawColorBar`, `applyColorScaleFromBarY`, mouse press/move/hover, floor clamp |
| `CHANGELOG.md` | Unreleased note |

`mainwindow.cpp` spinbox wiring stays.

## Testing (manual)

1. Release build.  
2. Default: full rainbow, no black/white pads.  
3. Drag ceil down to ~50 %, floor up to ~20 %: bar shows black / squeezed palette / white; spectrogram contrast matches.  
4. Drag the coloured middle: window slides, width stays; stops at 0 and 100 %.  
5. Click white or black: nearest edge jumps to the cursor.  
6. Double-click / Reset: back to 0–100 %.  
7. Spinbox 200 %: colour to the top of the bar, no white pad; spectrogram less sensitive.  
8. Selection / history wheel on the plot still work; bar drag does not start a selection.  
