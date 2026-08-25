# Spectrogram Time Zoom Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add in-widget spectrogram time zoom (max 1:1) and a 48 h load cap so a loaded `.rcsg` can be seen whole and a selection can cover hours, not just the 1:1 pane.

**Architecture:** One `float m_rowsPerPixel` on `SpectrumWaterfall`. `TimeView` maps widget Y ↔ buffer rows through that factor. At `rpp == 1` behaviour is today’s 1:1 bottom-aligned view. At `rpp > 1` each pixel is a max-pool bin of consecutive rows. Colour max is incremental via per-row `peak`. Load sets `m_historyCapUnlocked` so a long file is not trimmed to the live History combo.

**Tech Stack:** C++17, Qt 6 Widgets, existing `SpectrumWaterfall` / `MainWindow` (no new files, no test harness).

## Global Constraints

- Max zoom-in is **1 row = 1 pixel**; never stretch a row taller than 1 px.
- Zoom-out uses `rowsPerPixel ≥ 1`; downsample = **max** `displayRate` per channel in the bin.
- Ctrl+wheel zooms around the cursor row; unmodified wheel pans; colour-bar wheel stays colour.
- Fit all is a **one-shot** (not a lock); does not re-apply when live rows arrive.
- Load RAM cap **172 800 rows** (48 h at 1 s/row). Live History combo stays 15 min … 4 h (max 8 h).
- Do not full-scan the rate matrix on every `appendDisplayRow`.
- Disconnect must **not** `waterfall->clear()`. Reset spectrum **does**.
- `rowsPerPixel` is not saved in `QSettings`.
- No unit-test suite in this repo; each task ends with a Release compile and a manual check.
- Match existing style (`tr()`, Qt types, no drive-by refactors). User-facing strings via `tr()`.
- Work on `feature/spectrogram-history`. Do not commit `spectrograms/` or `.zip` recordings.

## File map

| File | Responsibility |
|------|----------------|
| `src/spectrumwaterfall.h` | `m_rowsPerPixel`, `m_historyCapUnlocked`, `Row::peak`, `TimeView` fields, public `fitAll` / `zoomOneToOne` / `setRowsPerPixel` |
| `src/spectrumwaterfall.cpp` | Time mapping, paint bins, Ctrl+wheel, selection Y, incremental colour max, load cap, context menu, badge, tooltip |
| `src/mainwindow.h` / `.cpp` | Fit all button; disconnect without clearing waterfall |
| `CHANGELOG.md` | Unreleased notes |
| `AGENTS.md` | Time-zoom + 48 h cap; fix stale “still planned” |
| Spec (read-only) | `docs/superpowers/specs/2026-08-25-spectrogram-time-zoom-design.md` |

---

### Task 1: TimeView math and zoom API (1:1 unchanged)

**Files:**
- Modify: `src/spectrumwaterfall.h` (`TimeView`, public zoom API, members, `kMaxRowsCap`)
- Modify: `src/spectrumwaterfall.cpp` (`timeView`, `maxScroll`, `rowFromWidgetY`, new helpers, `fitAll` / `zoomOneToOne` / `setRowsPerPixel`)

**Interfaces:**
- Consumes: existing `m_scrollFromNewest`, `m_rows`, `plotRect()`
- Produces:
  - `void setRowsPerPixel(float rpp, int anchorRow = -1, int anchorWidgetY = -1);`
  - `float rowsPerPixel() const;`
  - `float maxRowsPerPixel() const;`
  - `void fitAll();`
  - `void zoomOneToOne();`
  - `TimeView` with `visible` = **pixel rows** (image height), `firstRow` / `lastRow` = inclusive buffer range, plus `float rpp`
  - `int rowFromWidgetY(int y, const TimeView &tv) const` uses `rpp`
  - `int rowToWidgetY(int row, const TimeView &tv) const`

- [ ] **Step 1: Raise the row cap and add members in the header**

In `src/spectrumwaterfall.h`:

Change `kMaxRowsCap`:

```cpp
static constexpr int kMaxRowsCap = 172800; // 48 h @ 1 s / row
```

Extend `TimeView`:

```cpp
struct TimeView {
    QRect imgRect;
    QRect dest;
    int visible = 0;   // pixel rows (= dest height / viewport image height)
    int firstRow = 0;  // inclusive buffer index
    int lastRow = 0;   // inclusive buffer index
    float rpp = 1.0f;
};
```

Add public methods after `goToOldest()`:

```cpp
void setRowsPerPixel(float rpp, int anchorRow = -1, int anchorWidgetY = -1);
float rowsPerPixel() const { return m_rowsPerPixel; }
float maxRowsPerPixel() const;
void fitAll();
void zoomOneToOne();
```

Add private helpers next to `rowFromWidgetY`:

```cpp
int rowToWidgetY(int row, const TimeView &tv) const;
int binFirstRow(int pixelY, const TimeView &tv) const;
int binLastRowExclusive(int pixelY, const TimeView &tv) const;
void clampRowsPerPixel();
```

Add members with the other history fields:

```cpp
float m_rowsPerPixel = 1.0f;
bool m_historyCapUnlocked = false;
```

- [ ] **Step 2: Implement `maxRowsPerPixel`, `clampRowsPerPixel`, `timeView`, `maxScroll`**

```cpp
float SpectrumWaterfall::maxRowsPerPixel() const
{
    const QRect imgRect = plotRect().adjusted(1, 1, -1, -1);
    const int plotH = qMax(1, imgRect.height());
    const int nRows = m_rows.size();
    if (nRows <= plotH) {
        return 1.0f;
    }
    return float(nRows) / float(plotH);
}

void SpectrumWaterfall::clampRowsPerPixel()
{
    const float lo = 1.0f;
    const float hi = maxRowsPerPixel();
    m_rowsPerPixel = qBound(lo, m_rowsPerPixel, hi);
}

int SpectrumWaterfall::maxScroll() const
{
    const QRect imgRect = plotRect().adjusted(1, 1, -1, -1);
    const int plotH = qMax(1, imgRect.height());
    const int nRows = m_rows.size();
    const float rpp = qMax(1.0f, m_rowsPerPixel);
    const int visibleBuf = (rpp <= 1.0001f)
        ? qMin(nRows, plotH)
        : qMin(nRows, qMax(1, int(std::ceil(double(plotH) * double(rpp)))));
    return qMax(0, nRows - visibleBuf);
}
```

Replace `timeView` body so `rpp == 1` is bit-identical to today, `rpp > 1` fills the pane:

```cpp
bool SpectrumWaterfall::timeView(const QRect &plot, TimeView *tv) const
{
    if (!tv || m_rows.isEmpty() || m_channels <= 0) {
        return false;
    }
    const QRect imgRect = plot.adjusted(1, 1, -1, -1);
    if (imgRect.width() < 1 || imgRect.height() < 1) {
        return false;
    }

    const int nRows = m_rows.size();
    const int plotH = imgRect.height();
    const float rpp = qMax(1.0f, m_rowsPerPixel);

    if (rpp <= 1.0001f) {
        const int visible = qMin(nRows, plotH);
        if (visible < 1) {
            return false;
        }
        const int scroll = qBound(0, m_scrollFromNewest, qMax(0, nRows - visible));
        const int firstRow = nRows - visible - scroll;
        const int destTop = imgRect.top() + plotH - visible;
        tv->imgRect = imgRect;
        tv->dest = QRect(imgRect.left(), destTop, imgRect.width(), visible);
        tv->visible = visible;
        tv->firstRow = firstRow;
        tv->lastRow = firstRow + visible - 1;
        tv->rpp = 1.0f;
        return true;
    }

    const int pixelRows = plotH;
    const int visibleBuf = qMin(nRows, qMax(1, int(std::ceil(double(pixelRows) * double(rpp)))));
    const int scroll = qBound(0, m_scrollFromNewest, qMax(0, nRows - visibleBuf));
    const int firstRow = nRows - visibleBuf - scroll;
    const int lastRow = firstRow + visibleBuf - 1;
    tv->imgRect = imgRect;
    tv->dest = imgRect;
    tv->visible = pixelRows;
    tv->firstRow = firstRow;
    tv->lastRow = lastRow;
    tv->rpp = rpp;
    return true;
}
```

- [ ] **Step 3: Y mapping helpers**

Replace `rowFromWidgetY` and add:

```cpp
int SpectrumWaterfall::rowFromWidgetY(int y, const TimeView &tv) const
{
    if (tv.visible <= 0) {
        return 0;
    }
    const int local = std::clamp(y - tv.dest.top(), 0, tv.visible - 1);
    const int row = tv.firstRow + int(std::floor(double(local) * double(tv.rpp)));
    return std::clamp(row, tv.firstRow, tv.lastRow);
}

int SpectrumWaterfall::rowToWidgetY(int row, const TimeView &tv) const
{
    if (tv.rpp <= 1.0001f) {
        return tv.dest.top() + (row - tv.firstRow);
    }
    const int y = tv.dest.top()
        + int(std::floor(double(row - tv.firstRow) / double(tv.rpp)));
    return std::clamp(y, tv.dest.top(), tv.dest.bottom());
}

int SpectrumWaterfall::binFirstRow(int pixelY, const TimeView &tv) const
{
    const int local = std::clamp(pixelY, 0, tv.visible - 1);
    const int row = tv.firstRow + int(std::floor(double(local) * double(tv.rpp)));
    return std::clamp(row, tv.firstRow, tv.lastRow);
}

int SpectrumWaterfall::binLastRowExclusive(int pixelY, const TimeView &tv) const
{
    const int local = std::clamp(pixelY, 0, tv.visible - 1);
    const int row = tv.firstRow + int(std::floor(double(local + 1) * double(tv.rpp)));
    return std::clamp(row, tv.firstRow, tv.lastRow + 1);
}
```

- [ ] **Step 4: `setRowsPerPixel` / `fitAll` / `zoomOneToOne`**

```cpp
void SpectrumWaterfall::setRowsPerPixel(float rpp, int anchorRow, int anchorWidgetY)
{
    const float before = m_rowsPerPixel;
    m_rowsPerPixel = rpp;
    clampRowsPerPixel();

    if (anchorRow >= 0 && !m_rows.isEmpty()) {
        TimeView tv;
        // Provisional scroll 0 so dest geometry is known, then solve for scroll.
        const int saved = m_scrollFromNewest;
        m_scrollFromNewest = 0;
        if (timeView(plotRect(), &tv) && tv.visible > 0) {
            const int local = std::clamp(anchorWidgetY - tv.dest.top(), 0, tv.visible - 1);
            const int firstWanted =
                anchorRow - int(std::floor(double(local) * double(m_rowsPerPixel)));
            const int visibleBuf = tv.lastRow - tv.firstRow + 1;
            m_scrollFromNewest = m_rows.size() - visibleBuf - firstWanted;
        } else {
            m_scrollFromNewest = saved;
        }
    }

    clampScroll();
    if (qFuzzyCompare(m_rowsPerPixel, before) && anchorRow < 0) {
        return;
    }
    rebuildViewportImage();
    emitScrollSignals();
    update();
}

void SpectrumWaterfall::fitAll()
{
    if (m_rows.isEmpty()) {
        return;
    }
    m_rowsPerPixel = maxRowsPerPixel();
    m_scrollFromNewest = 0;
    clampRowsPerPixel();
    clampScroll();
    rebuildViewportImage();
    emitScrollSignals();
    update();
}

void SpectrumWaterfall::zoomOneToOne()
{
    if (m_rows.isEmpty() || m_rowsPerPixel <= 1.0001f) {
        setRowsPerPixel(1.0f);
        return;
    }
    TimeView tv;
    int anchorRow = -1;
    int anchorY = -1;
    if (timeView(plotRect(), &tv)) {
        anchorY = tv.dest.center().y();
        anchorRow = rowFromWidgetY(anchorY, tv);
    }
    setRowsPerPixel(1.0f, anchorRow, anchorY);
}
```

`clear()` must reset `m_rowsPerPixel = 1.0f` and `m_historyCapUnlocked = false`, then `recomputeCapacity()` so live cap returns after Reset.

- [ ] **Step 5: Compile**

```bash
cmake --build build --config Release
```

If `build/` is missing, configure first with the local Qt prefix. Expected: build succeeds. At `rpp == 1` the waterfall must look and scroll as before (no Fit all UI yet).

- [ ] **Step 6: Commit**

```bash
git add src/spectrumwaterfall.h src/spectrumwaterfall.cpp
git commit -m "feat: spectrogram TimeView zoom factor (1:1 default)"
```

---

### Task 2: Downsampled paint, ticks, badge

**Files:**
- Modify: `src/spectrumwaterfall.cpp` (`rebuildViewportImage`, `ensureViewportImage` callers, `appendDisplayRow` live path, `paintEvent` ticks + badge, tooltip)

**Interfaces:**
- Consumes: `TimeView::{visible,firstRow,lastRow,rpp}`, `binFirstRow`, `binLastRowExclusive`, `rowToWidgetY`
- Produces: viewport image height = `tv.visible` pixels; each scanline is max-pool of its bin; 1:1 path still one row per pixel

- [ ] **Step 1: Paint one image row from a bin**

Add a private method in the header:

```cpp
void paintImageRow(int pixelY, const TimeView &tv);
```

```cpp
void SpectrumWaterfall::paintImageRow(int pixelY, const TimeView &tv)
{
    if (m_image.isNull() || pixelY < 0 || pixelY >= m_image.height()) {
        return;
    }
    auto *line = reinterpret_cast<QRgb *>(m_image.scanLine(pixelY));
    const int r0 = binFirstRow(pixelY, tv);
    const int r1 = binLastRowExclusive(pixelY, tv);
    for (int x = 0; x < m_channels; ++x) {
        float mx = 0.0f;
        for (int r = r0; r < r1; ++r) {
            const Row &row = m_rows.at(r);
            const float live = (x < row.rates.size()) ? row.rates[x] : 0.0f;
            mx = std::max(mx, displayRate(live, x));
        }
        line[x] = rateToColor(mx);
    }
}
```

At `rpp == 1`, `r1 == r0 + 1`, same as today’s single-row loop.

- [ ] **Step 2: `rebuildViewportImage` uses pixel rows + bins**

```cpp
void SpectrumWaterfall::rebuildViewportImage()
{
    TimeView tv;
    if (!timeView(plotRect(), &tv) || m_channels <= 0) {
        m_image = QImage();
        m_viewportFirstRow = -1;
        m_viewportCount = 0;
        return;
    }

    ensureViewportImage(tv.visible);
    if (m_image.isNull()) {
        return;
    }

    for (int i = 0; i < tv.visible; ++i) {
        paintImageRow(i, tv);
    }
    m_viewportFirstRow = tv.firstRow;
    m_viewportCount = tv.visible;
}
```

- [ ] **Step 3: Live follow incremental path**

In `appendDisplayRow`, today’s `memmove` + last scanline is valid **only** when `m_rowsPerPixel <= 1.0001f` and `tv.visible` is 1:1 rows.

When `rpp > 1` and following live and scale is stable: **do not** call `rebuildViewportImage()`. Recompute **only** the last image row:

```cpp
    if (m_rowsPerPixel > 1.0001f && wasFollow && !scaleChanged) {
        TimeView tv;
        if (timeView(plotRect(), &tv) && !m_image.isNull()
            && m_image.height() == tv.visible && m_image.width() == m_channels) {
            paintImageRow(tv.visible - 1, tv);
            m_viewportFirstRow = tv.firstRow;
            m_viewportCount = tv.visible;
            refreshCursorAfterScroll(droppedOldest);
            emitScrollSignals();
            update();
            return;
        }
    }
```

If `rpp` / size / colour-max changed, fall through to `rebuildViewportImage()`.

- [ ] **Step 4: Time ticks and badge**

In `paintEvent`, Y ticks currently use `tv.firstRow + tv.visible / 2` and `y = tv.dest.top() + (rowIdx - tv.firstRow)`. Change ticks to `firstRow`, midpoint `(firstRow+lastRow)/2`, `lastRow`, and `y = rowToWidgetY(rowIdx, tv)`.

Badge (existing top-left overlay): append time scale after Live/Net / hist:

```cpp
        QString scale;
        if (tv.rpp <= 1.0001f) {
            scale = tr("1:1");
        } else {
            const QDateTime t0 = m_rows.at(tv.firstRow).wallTime;
            const QDateTime t1 = m_rows.at(tv.lastRow).wallTime;
            if (t0.isValid() && t1.isValid()) {
                const qint64 sec = qAbs(t0.secsTo(t1));
                if (sec >= 3600) {
                    scale = tr("~%1 h").arg(QString::number(double(sec) / 3600.0, 'f', 1));
                } else {
                    scale = tr("~%1 min").arg(qMax(1, int((sec + 30) / 60)));
                }
            } else {
                quint32 sum = 0;
                for (int r = tv.firstRow; r <= tv.lastRow; ++r) {
                    sum += m_rows.at(r).intervalSec;
                }
                scale = tr("~%1 min").arg(qMax(1, int((sum + 30) / 60)));
            }
        }
        badge = tr("%1 · %2").arg(badge, scale);
```

Update constructor tooltip: add `Ctrl+wheel: zoom time (max 1:1) · Fit all / Zoom 1:1 in the menu.` Keep “Time is 1:1 (one row = one pixel)” as the **maximum** zoom, not the only mode.

- [ ] **Step 5: PNG view uses buffer span, not pixel count**

In `exportAsPng` for `PngExportScope::View`:

```cpp
        firstRow = tv.firstRow;
        nRows = tv.lastRow - tv.firstRow + 1;
```

If `nRows >= 2000`, show the same size-confirm dialog already used for full history (View can be huge after Fit all).

- [ ] **Step 6: Compile + commit**

```bash
cmake --build build --config Release
git add src/spectrumwaterfall.h src/spectrumwaterfall.cpp
git commit -m "feat: max-pool spectrogram rows when zoomed out"
```

---

### Task 3: Ctrl+wheel, Fit all / Zoom 1:1 menu

**Files:**
- Modify: `src/spectrumwaterfall.cpp` (`wheelEvent`, `contextMenuEvent`)
- Modify: `src/mainwindow.h` (`m_waterfallFitAllBtn`)
- Modify: `src/mainwindow.cpp` (button next to Follow live)

**Interfaces:**
- Consumes: `setRowsPerPixel`, `fitAll`, `zoomOneToOne`, `maxRowsPerPixel`
- Produces: user can zoom and Fit all

- [ ] **Step 1: `wheelEvent`**

After the colour-bar branch, before pan:

```cpp
    if (event->modifiers() & Qt::ControlModifier) {
        if (m_rows.isEmpty()) {
            event->ignore();
            return;
        }
        TimeView tv;
        const QPoint pos = event->position().toPoint();
        int anchorRow = -1;
        if (timeView(plotRect(), &tv)) {
            anchorRow = rowFromWidgetY(pos.y(), tv);
        }
        const float factor = (delta > 0) ? (1.0f / 1.25f) : 1.25f;
        setRowsPerPixel(m_rowsPerPixel * factor, anchorRow, pos.y());
        event->accept();
        return;
    }
```

Unmodified wheel stays `setScrollFromNewest(m_scrollFromNewest + dir * steps)`.

- [ ] **Step 2: Context menu**

After “Go to oldest”, add:

```cpp
    QAction *fitAllAct = menu.addAction(tr("Fit all"));
    fitAllAct->setEnabled(!m_rows.isEmpty());
    fitAllAct->setToolTip(tr("Show the entire history buffer in the pane (time zoom-out).\n"
                             "Maximum zoom-in stays 1 row = 1 pixel."));

    QAction *oneToOneAct = menu.addAction(tr("Zoom 1:1"));
    oneToOneAct->setEnabled(!m_rows.isEmpty() && m_rowsPerPixel > 1.0001f);
    oneToOneAct->setToolTip(tr("One history row = one screen pixel. Centres on the current view."));
```

Handle `chosen == fitAllAct` → `fitAll()`; `chosen == oneToOneAct` → `zoomOneToOne()`.

- [ ] **Step 3: Fit all button in setup**

In `mainwindow.h` next to `m_waterfallLiveBtn`:

```cpp
    QPushButton *m_waterfallFitAllBtn = nullptr;
```

In `mainwindow.cpp` immediately after `lay->addWidget(m_waterfallLiveBtn);`:

```cpp
        m_waterfallFitAllBtn = new QPushButton(tr("Fit all"), box);
        m_waterfallFitAllBtn->setToolTip(
            tr("Show the entire spectrogram history in the pane (time zoom-out).\n"
               "Ctrl+wheel zooms toward 1:1 (one row = one pixel)."));
        lay->addWidget(m_waterfallFitAllBtn);
```

Connect:

```cpp
    connect(m_waterfallFitAllBtn, &QPushButton::clicked, this, [this] {
        if (m_waterfall) {
            m_waterfall->fitAll();
        }
    });
```

- [ ] **Step 4: Compile, manual check, commit**

Load `spectrograms/RC-110-004894/2026-08-13.rcsg` (untracked, do not git-add). Fit all should fill the pane; Ctrl+wheel in should approach 1:1; wheel without Ctrl pans.

```bash
cmake --build build --config Release
git add src/spectrumwaterfall.cpp src/mainwindow.h src/mainwindow.cpp
git commit -m "feat: Ctrl+wheel time zoom and Fit all"
```

---

### Task 4: Selection Y mapping at zoom

**Files:**
- Modify: `src/spectrumwaterfall.cpp` (`selectionPixelRect`; `rowAtPlotY` if it still assumes 1:1)

**Interfaces:**
- Consumes: `rowFromWidgetY`, `rowToWidgetY`
- Produces: rectangle / move / resize operate in buffer rows at any `rpp`

- [ ] **Step 1: `selectionPixelRect` Y**

Replace the 1:1 Y lines:

```cpp
    const int top = rowToWidgetY(row0, tv);
    const int bottom = rowToWidgetY(row1, tv);
```

If `top == bottom`, use `QRect(..., QSize(width, 1))` so a one-bin box is still 1 px tall. Existing `kSelectEdgeHitPx = 7` stays.

- [ ] **Step 2: `rowAtPlotY`**

It currently does `return tv.firstRow + local`. Delegate:

```cpp
int SpectrumWaterfall::rowAtPlotY(int y, const QRect &plot) const
{
    TimeView tv;
    if (!timeView(plot, &tv)) {
        return -1;
    }
    if (y < tv.dest.top() || y > tv.dest.bottom()) {
        return -1;
    }
    return rowFromWidgetY(y, tv);
}
```

Move/resize already uses `rowFromWidgetY` for `dRow` — once that helper includes `rpp`, pixel drag maps to many buffer rows. No extra `* rpp` on `dRow`.

- [ ] **Step 3: Compile, manual check, commit**

Fit all → drag a tall box → Spectrum from selection duration ≈ selected wall span; zoom in → box stays on the same rows.

```bash
cmake --build build --config Release
git add src/spectrumwaterfall.cpp
git commit -m "fix: map spectrogram selection through time zoom"
```

---

### Task 5: Incremental colour max

**Files:**
- Modify: `src/spectrumwaterfall.h` (`Row::peak`)
- Modify: `src/spectrumwaterfall.cpp` (`appendDisplayRow`, `setBackground`, `clearBackground`, `setDisplayMode`, `resetColorScale`, `loadHistory`, helpers)

**Interfaces:**
- Consumes: `displayRate`, existing `kScaleHysteresis`
- Produces: `float Row::peak`; `float rowPeak(const Row &) const`; `void recomputeAllRowPeaks()`; `float maxOfRowPeaks() const`

- [ ] **Step 1: Add `peak` and helpers**

In `struct Row`:

```cpp
        float peak = 0.0f;          // max displayRate on this row (current Live/Net)
```

```cpp
float SpectrumWaterfall::rowPeakValue(const Row &row) const
{
    float mx = 0.0f;
    const int n = qMin(m_channels, row.rates.size());
    for (int ch = 0; ch < n; ++ch) {
        mx = std::max(mx, displayRate(row.rates[ch], ch));
    }
    return mx;
}

void SpectrumWaterfall::recomputeAllRowPeaks()
{
    float mx = 1e-6f;
    for (Row &row : m_rows) {
        row.peak = rowPeakValue(row);
        mx = std::max(mx, row.peak);
    }
    m_displayMax = mx;
}

float SpectrumWaterfall::maxOfRowPeaks() const
{
    float mx = 1e-6f;
    for (const Row &row : m_rows) {
        mx = std::max(mx, row.peak);
    }
    return mx;
}
```

Keep `matrixMaxRate()` as a thin wrapper around `maxOfRowPeaks()` **or** delete call sites. Do not walk channels×rows except inside `rowPeakValue` / load.

- [ ] **Step 2: Append / drop without a full matrix walk**

In `appendDisplayRow`, **before** `m_rows.append`:

```cpp
    row.peak = rowPeakValue(row);
```

After the drop loop, replace `matrixMaxRate()`:

```cpp
    const float oldMax = m_displayMax;
    if (dropped > 0) {
        bool needRescan = false;
        // dropped rows are gone; if any dropped peak was near the old max, rescan peaks only
        // Track dropped peaks while removing:
    }
```

Concrete drop loop:

```cpp
    float droppedPeak = 0.0f;
    int dropped = 0;
    while (m_rows.size() > m_maxRows) {
        droppedPeak = std::max(droppedPeak, m_rows.first().peak);
        m_rows.removeFirst();
        ++dropped;
    }

    row.peak = rowPeakValue(row); // if not set before append — set on the Row before append
```

Set `peak` on the local `Row` **before** `m_rows.append`.

Scale:

```cpp
    const float oldMax = m_displayMax;
    float newMax = oldMax;
    if (row.peak > oldMax * (1.0f + kScaleHysteresis) || oldMax < 1e-6f) {
        newMax = std::max(oldMax, row.peak);
    }
    if (dropped > 0
        && droppedPeak >= oldMax * (1.0f - kScaleHysteresis)) {
        newMax = maxOfRowPeaks();
    }
```

Then the existing `scaleChanged` hysteresis compare using `newMax` vs `oldMax`. **Never** call `matrixMaxRate()` here.

- [ ] **Step 3: Rare full peak recompute**

`setBackground`, `clearBackground`, `setDisplayMode`, `resetColorScale`: replace `m_displayMax = matrixMaxRate()` with `recomputeAllRowPeaks()`.

- [ ] **Step 4: Compile + commit**

```bash
cmake --build build --config Release
git add src/spectrumwaterfall.h src/spectrumwaterfall.cpp
git commit -m "perf: incremental spectrogram colour max via per-row peaks"
```

---

### Task 6: Load cap, unlock, disconnect

**Files:**
- Modify: `src/spectrumwaterfall.cpp` (`recomputeCapacity`, `loadHistory`, `pushSpectrum` channel mismatch)
- Modify: `src/mainwindow.cpp` (`onDisconnected`)

**Interfaces:**
- Consumes: `kMaxRowsCap`, `fitAll()`, `m_historyCapUnlocked`
- Produces: long `.rcsg` keeps all rows up to 172 800; History combo is the only user trim of an unlocked buffer

- [ ] **Step 1: `recomputeCapacity` respects unlock**

```cpp
void SpectrumWaterfall::recomputeCapacity()
{
    if (m_historyCapUnlocked) {
        m_maxRows = kMaxRowsCap;
        clampScroll();
        return;
    }
    const int secPerRow = qMax(1, m_integrate);
    const int rows = int((qint64(m_historyMinutes) * 60) / secPerRow);
    m_maxRows = qBound(kMinRows, rows, kMaxRowsCap);
    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
    }
    clampScroll();
}
```

`setHistoryMinutes`: **before** changing minutes, `m_historyCapUnlocked = false;` then existing `recomputeCapacity()` (this is the intentional trim).

`setIntegrateCount`: do **not** clear the unlock flag (spec).

`clear()`: `m_historyCapUnlocked = false;` `m_rowsPerPixel = 1.0f;` then `recomputeCapacity()`.

Channel mismatch in `pushSpectrum` (the `n != m_channels` wipe): also `m_historyCapUnlocked = false;` `m_rowsPerPixel = 1.0f;` `recomputeCapacity();` so live returns to the History combo cap.

- [ ] **Step 2: `loadHistory` — no minutes trim, cap, wait cursor, Fit all**

After the file dialog returns a path, wrap load/gunzip:

```cpp
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    // ... existing gunzip + SpectrogramFile::load ...
    QGuiApplication::restoreOverrideCursor();
```

Use try/finally-style: restore cursor on every return path (including failures).

Do **not** assign `m_historyMinutes` from the file in a way that calls `recomputeCapacity()` before rows are filled such that they get trimmed. Sequence:

```cpp
    m_rows.clear();
    m_channels = int(doc.nChannels);
    // calibration, integrate as today
    if (!doc.serial.isEmpty()) {
        m_deviceSerial = doc.serial;
    }

    const int nDoc = doc.rows.size();
    const int start = (nDoc > kMaxRowsCap) ? (nDoc - kMaxRowsCap) : 0;
    const int nKeep = nDoc - start;

    m_historyCapUnlocked = true;
    m_maxRows = kMaxRowsCap;

    m_rows.reserve(nKeep);
    for (int i = start; i < nDoc; ++i) {
        const SpectrogramFile::Row &r = doc.rows.at(i);
        Row row;
        row.rates = r.rates;
        row.deltas = r.deltas;
        row.liveTimeSec = r.liveTimeSec;
        row.intervalSec = r.intervalSec;
        row.wallTime = r.wallTime;
        row.peak = rowPeakValue(row);
        m_rows.append(std::move(row));
    }

    m_hasBaseline = false;
    m_baseline = Snapshot{};
    m_integrateProgress = 0;
    m_scrollFromNewest = 0;
    m_displayMax = maxOfRowPeaks();
    // ... reset viewport / cursor as today ...

    fitAll(); // includes rebuild + follow-live scroll 0
    emit followLiveChanged(true);
    emitScrollSignals();
```

If `nDoc > kMaxRowsCap`, after load show:

```cpp
        tr("Loaded %1 of %2 rows (limit 48 h at 1 s/row).")
            .arg(nKeep).arg(nDoc)
```

in the existing information (or warning) box together with channel/serial lines.

Keep `m_historyMinutes` from the file only as metadata for save (`doc.historyMinutes` on next save can stay the combo value, or the loaded header). Do not let it shrink `m_maxRows` while unlocked.

Need `#include <QGuiApplication>` if not already pulled via `QApplication`.

- [ ] **Step 3: Disconnect keeps waterfall**

In `MainWindow::onDisconnected`, **delete** these two lines only:

```cpp
        m_waterfall->setDeviceSerial(QString());
        m_waterfall->clear();
```

Keep `m_spectrogramRecorder->stop()`, spectrum plot clear, labels. Leave device serial on the waterfall (PNG / extract metadata still useful). If serial must clear for a new device, set it again in `onConnected` (already `setDeviceSerial(m_device->serialNumber())`).

- [ ] **Step 4: Compile, manual check, commit**

Load the local `.rcsg` (~2300 rows): row count in the dialog matches the file (not 120 min). Fit all runs automatically. Change History combo → buffer may trim (expected). Disconnect with a device must not wipe (when a device is available).

```bash
cmake --build build --config Release
git add src/spectrumwaterfall.cpp src/mainwindow.cpp
git commit -m "feat: load spectrogram history up to 48 h without trimming"
```

---

### Task 7: Changelog and AGENTS.md

**Files:**
- Modify: `CHANGELOG.md` Unreleased
- Modify: `AGENTS.md` waterfall / planned sections

**Interfaces:**
- Consumes: behaviour from tasks 1–6
- Produces: docs match the product

- [ ] **Step 1: CHANGELOG Unreleased Added**

```markdown
- **Spectrogram time zoom:** Ctrl+wheel zooms time around the cursor (max **1:1**,
  one row = one pixel). **Fit all** (button + context menu) shows the whole buffer;
  **Zoom 1:1** recentres. Zoom-out bins rows with max rate (hotspots stay visible).
  Badge shows `1:1` or the visible span (e.g. `~6 h`).
- Load spectrogram history keeps up to **48 h** at 1 s/row (172 800 rows); no longer
  trims to the live History combo. Files longer than the cap load the newest rows
  and warn. Disconnect does not clear the spectrogram buffer (Reset spectrum does).
```

- [ ] **Step 2: AGENTS.md**

Replace the 1:1-only waterfall bullet with: 1:1 is the **maximum** zoom; zoom-out is `rowsPerPixel` max-pool; do not stretch rows above 1 px.

Add under Spectrogram I/O: load cap 172 800; `m_historyCapUnlocked`; incremental `Row::peak` colour max.

**Still planned** — remove the two items this branch already shipped (continuous recording, rectangle → spectrum/MCS). Leave:

- ROI definition from waterfall selection
- File-backed week/month timeline
- Seamless RAM resume from today’s `.rcsg` on startup

- [ ] **Step 3: Commit**

```bash
git add CHANGELOG.md AGENTS.md
git commit -m "docs: spectrogram time zoom and 48 h load cap"
```

---

## Spec coverage

| Spec section | Task |
|--------------|------|
| §1 Time model, 1:1 max, Ctrl+wheel, Fit all, Zoom 1:1 | 1, 3 |
| §2 Paint max-pool, incremental `rpp>1` last line, selection mapping | 2, 4 |
| §3 Load cap 172800, no minutes trim, unlock, wait cursor, auto Fit all | 6 |
| §4 Live append, disconnect keep, reset clear, acquisition untouched | 6 (`clear` / mismatch / disconnect) |
| §5 Incremental colour max, per-row peak | 5 |
| §6 UI Fit all button, menu, badge, tooltip | 2, 3 |
| Errors: over-cap warning, empty Fit all no-op | 1 (`fitAll`), 6 |
| PNG view buffer span | 2 |
| CHANGELOG / AGENTS | 7 |

## Placeholder / type check

Names used everywhere: `m_rowsPerPixel`, `m_historyCapUnlocked`, `fitAll()`, `zoomOneToOne()`, `setRowsPerPixel(float, int, int)`, `rowToWidgetY`, `binFirstRow`, `binLastRowExclusive`, `rowPeakValue`, `recomputeAllRowPeaks`, `maxOfRowPeaks`, `Row::peak`, `kMaxRowsCap = 172800`.
