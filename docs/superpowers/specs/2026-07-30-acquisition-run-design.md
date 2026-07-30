# Acquisition Run (Time | Total counts) — Design

**Date:** 2026-07-30  
**App:** radiacode-monitor  
**Status:** approved to implement (brainstorm follow-up)

## Goal

Timed or count-based spectrum acquisition with clear Start/Stop and progress. Foundation for later Background/Net and waterfall; **not** in this slice: BG library, net view, waterfall, ROI-count target.

## User story

While connected, operator chooses:

- **Time** — stop when device spectrum live time (`durationSec`) ≥ N seconds, or  
- **Total counts** — stop when Σ channel counts ≥ N  

If spectrum already has data, Start opens a dialog: **Continue…** (no reset), **Save…**, **Reset and start**, or **Cancel**. Empty spectrum starts immediately. On target reached: stop run, log, status message; spectrum stays on screen for Save.

## UI (Live group)

New row/group **Acquisition**:

| Control | Behavior |
|---------|----------|
| Mode | Radio or combo: Time (s) / Total counts |
| Target | `QSpinBox` — time 1…86400 s; counts 1…2e9 |
| **Start** / **Stop** | Start only when Connected & idle; Stop when running |
| Progress | bar + e.g. `45230 / 100000 (45%)` or `120 / 300 s (40%)` |
| Start dialog (if spectrum has data) | **Continue…** / **Save…** / **Reset and start** / **Cancel** |

Disabled when disconnected. Disconnect aborts run (not “completed”).

## Logic

```
Start:
  if reset: spectrumReset → wait operationFinished → (optional short settle) → mark running
  else: mark running immediately
  log "Acquisition started: …"

onSpectrum (while running):
  total = sum(counts)
  live = durationSec
  update progress
  if mode==Time && live >= target → complete
  if mode==Counts && total >= target → complete

complete:
  running=false
  log "Acquisition complete: …"
  statusBar message

Stop (user):
  running=false
  log "Acquisition stopped"
```

**Overshoot:** stop on first spectrum that meets/exceeds target (one poll frame). Document in tooltip.

**Time base:** device `RcSpectrum::durationSec` (accumulation clock), not wall clock — correct after reset; matches spectrum live time label.

## Non-goals (this PR)

- Save-as-Background / net spectrum  
- Auto-save file on complete  
- ROI count target  
- Waterfall  
- Pause/resume  

## Files

| Path | Role |
|------|------|
| `src/acquisition/acquisitioncontroller.h/.cpp` | State machine + progress math |
| `src/mainwindow.*` | UI + wire to device/spectrum |
| `CMakeLists.txt` | sources |
| `CHANGELOG.md` / `README.md` | note feature |

## Testing (manual)

1. Connect USB, Start counts 1000, reset on → progress rises, auto-stop, log complete.  
2. Start time 30 s → stop near 30 s live time.  
3. Stop mid-run → cancelled.  
4. Disconnect mid-run → aborted, UI idle.  
5. Start without reset → continues from current spectrum totals.
