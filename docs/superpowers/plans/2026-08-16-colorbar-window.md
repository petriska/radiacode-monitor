# Colour-bar window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redraw the waterfall colour bar as black / squeezed palette / white between floor and ceil, with edge-drag and mid-drag of the whole window (SDR Console style).

**Architecture:** All behaviour stays in `SpectrumWaterfall`. Bar Y maps to 0…auto-max (not 0…200 %). Painting samples existing `rateToColor(frac * displayMax)` so the bar matches the spectrogram. Mouse hit-test chooses floor, ceil, or window drag. `rateToColor` and the Live-panel spinbox stay as they are.

**Tech Stack:** Qt 6 Widgets, C++17, CMake. No new files, no unit-test suite (project convention: Release build + manual check).

**Spec:** `docs/superpowers/specs/2026-08-16-colorbar-window-design.md`

---

## File map

| File | Responsibility |
|------|----------------|
| `src/spectrumwaterfall.h` | Drag mode, bar Y helpers, `setColorWindow`, drop `kColorFloorMax` |
| `src/spectrumwaterfall.cpp` | Paint, hit-test, mouse, floor clamp |
| `CHANGELOG.md` | Unreleased note |

---

### Task 1: Header API — drag mode, bar Y, window setter

**Files:**
- Modify: `src/spectrumwaterfall.h`

- [ ] **Step 1: Replace floor cap and add helpers**

In `src/spectrumwaterfall.h`, next to the other colour-scale public methods (after `resetColorScale()`), add:

```cpp
    /// Set floor and ceil together (window drag). Applies the same clamps as the setters.
    void setColorWindow(float floorFrac, float ceilFrac);
```

Replace the constants and drag state (around `kColorCeilMin` / `m_colorBarDragFloor`):

```cpp
    static constexpr float kColorCeilMin = 0.02f;
    static constexpr float kColorCeilMax = 2.0f;
    static constexpr float kColorMinGapFrac = 0.05f; // floor ≤ ceil × (1 − this)
    static constexpr int kColorBarEdgeHitPx = 6;

    enum class ColorBarDrag { None, Floor, Ceil, Window };
```

Remove `kColorFloorMax`.

Replace private colour-bar methods and members:

```cpp
    QRect colorBarRect() const;
    void drawColorBar(QPainter &p, const QRect &plot) const;
    float fracFromBarY(int y) const;          // 0 at bottom, 1 at top (auto-max)
    int yFromBarFrac(float frac) const;       // visual; frac clamped to [0, 1]
    ColorBarDrag hitTestColorBar(const QPoint &pos) const;
    void applyColorBarDrag(int y);
    void setColorBarHoverCursor(const QPoint &pos);

    // ... other members ...

    bool m_colorBarDragging = false;
    ColorBarDrag m_colorBarDragMode = ColorBarDrag::None;
    float m_windowDragFloor0 = 0.0f;
    float m_windowDragCeil0 = 1.0f;
    float m_windowDragPressFrac = 0.0f;
```

Delete `applyColorScaleFromBarY` and `m_colorBarDragFloor`.

- [ ] **Step 2: Commit**

```bash
git add src/spectrumwaterfall.h
git commit -m "wip: colour-bar window API (header)"
```

---

### Task 2: Fraction mapping and clamps

**Files:**
- Modify: `src/spectrumwaterfall.cpp`

- [ ] **Step 1: Bar Y ↔ fraction**

Add next to `colorBarRect()`:

```cpp
float SpectrumWaterfall::fracFromBarY(int y) const
{
    const QRect bar = colorBarRect();
    if (bar.height() <= 1) {
        return 0.0f;
    }
    const float t = std::clamp(float(y - bar.top()) / float(bar.height() - 1), 0.0f, 1.0f);
    return 1.0f - t;
}

int SpectrumWaterfall::yFromBarFrac(float frac) const
{
    const QRect bar = colorBarRect();
    const float n = std::clamp(frac, 0.0f, 1.0f);
    return bar.bottom() - int(std::lround(double(n) * double(bar.height() - 1)));
}
```

Expected: y at `bar.bottom()` → frac 0; y at `bar.top()` → frac 1. `yFromBarFrac(1.2)` equals `yFromBarFrac(1.0)` (top).

- [ ] **Step 2: Relax floor clamp; add `setColorWindow`**

In `setColorCeilFraction`, keep ceil clamp `kColorCeilMin…kColorCeilMax`. When pulling ceil down, still keep floor below it:

```cpp
    float floor = m_colorFloorFrac;
    const float maxFloor = f * (1.0f - kColorMinGapFrac);
    if (floor > maxFloor) {
        floor = std::max(0.0f, maxFloor);
    }
```

In `setColorFloorFraction`, **drop `kColorFloorMax`**:

```cpp
void SpectrumWaterfall::setColorFloorFraction(float frac)
{
    const float maxFloor = m_colorCeilFrac * (1.0f - kColorMinGapFrac);
    const float f = std::clamp(frac, 0.0f, maxFloor);
    if (qAbs(f - m_colorFloorFrac) < 1e-6f) {
        return;
    }
    m_colorFloorFrac = f;
    rebuildViewportImage();
    update();
    emit colorScaleChanged(m_colorFloorFrac, m_colorCeilFrac);
}
```

Add atomic window setter (one rebuild / one signal):

```cpp
void SpectrumWaterfall::setColorWindow(float floorFrac, float ceilFrac)
{
    const float c = std::clamp(ceilFrac, kColorCeilMin, kColorCeilMax);
    const float maxFloor = c * (1.0f - kColorMinGapFrac);
    const float f = std::clamp(floorFrac, 0.0f, maxFloor);
    if (qAbs(c - m_colorCeilFrac) < 1e-6f && qAbs(f - m_colorFloorFrac) < 1e-6f) {
        return;
    }
    m_colorCeilFrac = c;
    m_colorFloorFrac = f;
    rebuildViewportImage();
    update();
    emit colorScaleChanged(m_colorFloorFrac, m_colorCeilFrac);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/spectrumwaterfall.cpp
git commit -m "fix: allow colour-scale floor anywhere below ceil"
```

---

### Task 3: Paint compressed bar

**Files:**
- Modify: `src/spectrumwaterfall.cpp` — `drawColorBar`

- [ ] **Step 1: Sample `rateToColor` along 0…auto-max**

Replace the body of `drawColorBar` so each pixel row is `rateToColor(fracFromBarY(y) * m_displayMax)`. That automatically yields black below floor, full squeezed palette inside, white above ceil (and no white pad when `ceilFrac > 1`).

```cpp
void SpectrumWaterfall::drawColorBar(QPainter &p, const QRect &plot) const
{
    Q_UNUSED(plot);
    const QRect bar = colorBarRect();
    if (bar.height() < 4) {
        return;
    }

    for (int i = 0; i < bar.height(); ++i) {
        const int y = bar.top() + i;
        const float frac = fracFromBarY(y);
        p.setPen(QColor::fromRgb(rateToColor(frac * m_displayMax)));
        p.drawLine(bar.left(), y, bar.right(), y);
    }
    p.setPen(QColor(90, 94, 100));
    p.setBrush(Qt::NoBrush);
    p.drawRect(bar);

    const int yCeil = yFromBarFrac(m_colorCeilFrac);
    const int yFloor = yFromBarFrac(m_colorFloorFrac);
    p.setPen(QPen(QColor(255, 240, 180), 2));
    p.drawLine(bar.left() - 1, yCeil, bar.right() + 1, yCeil);
    if (m_colorFloorFrac > 1e-4f) {
        p.setPen(QPen(QColor(180, 200, 255), 2));
        p.drawLine(bar.left() - 1, yFloor, bar.right() + 1, yFloor);
    }
}
```

Delete the old full-palette loop and `frac / kColorCeilMax` marker mapping.

- [ ] **Step 2: Build**

```bash
cmake --build build --target radiacode-monitor -j$(nproc)
```

Expected: compiles. Default look: full rainbow (floor 0, ceil 1) with only the cream tick at the top.

- [ ] **Step 3: Commit**

```bash
git add src/spectrumwaterfall.cpp
git commit -m "feat: compress colour bar between floor and ceil"
```

---

### Task 4: Hit-test and mouse (edges + window drag)

**Files:**
- Modify: `src/spectrumwaterfall.cpp`

- [ ] **Step 1: Hit-test and drag apply**

```cpp
SpectrumWaterfall::ColorBarDrag SpectrumWaterfall::hitTestColorBar(const QPoint &pos) const
{
    const QRect bar = colorBarRect();
    if (!bar.contains(pos)) {
        return ColorBarDrag::None;
    }
    const int yCeil = yFromBarFrac(m_colorCeilFrac);
    const int yFloor = yFromBarFrac(m_colorFloorFrac);
    if (std::abs(pos.y() - yCeil) <= kColorBarEdgeHitPx) {
        return ColorBarDrag::Ceil;
    }
    if (std::abs(pos.y() - yFloor) <= kColorBarEdgeHitPx) {
        return ColorBarDrag::Floor;
    }
    const int yTop = std::min(yCeil, yFloor);
    const int yBot = std::max(yCeil, yFloor);
    if (pos.y() >= yTop && pos.y() <= yBot) {
        return ColorBarDrag::Window;
    }
    return (std::abs(pos.y() - yCeil) <= std::abs(pos.y() - yFloor))
        ? ColorBarDrag::Ceil
        : ColorBarDrag::Floor;
}

void SpectrumWaterfall::applyColorBarDrag(int y)
{
    const float frac = fracFromBarY(y);
    switch (m_colorBarDragMode) {
    case ColorBarDrag::Floor:
        setColorFloorFraction(frac);
        break;
    case ColorBarDrag::Ceil:
        setColorCeilFraction(frac);
        break;
    case ColorBarDrag::Window: {
        const float d = frac - m_windowDragPressFrac;
        float floor = m_windowDragFloor0 + d;
        float ceil = m_windowDragCeil0 + d;
        const float width = m_windowDragCeil0 - m_windowDragFloor0;
        const float spanMax = std::max(1.0f, m_windowDragCeil0);
        if (floor < 0.0f) {
            floor = 0.0f;
            ceil = width;
        }
        if (ceil > spanMax) {
            ceil = spanMax;
            floor = spanMax - width;
        }
        setColorWindow(floor, ceil);
        break;
    }
    case ColorBarDrag::None:
        break;
    }
}

void SpectrumWaterfall::setColorBarHoverCursor(const QPoint &pos)
{
    switch (hitTestColorBar(pos)) {
    case ColorBarDrag::Window:
        setCursor(Qt::SizeAllCursor);
        break;
    case ColorBarDrag::Floor:
    case ColorBarDrag::Ceil:
        setCursor(Qt::SizeVerCursor);
        break;
    case ColorBarDrag::None:
        break;
    }
}
```

Delete `applyColorScaleFromBarY`.

- [ ] **Step 2: Wire mouse press / move / hover**

`mousePressEvent` colour-bar branch — replace the 55 % split:

```cpp
    if (event->button() == Qt::LeftButton && colorBarRect().contains(event->pos())) {
        setFocus(Qt::MouseFocusReason);
        m_colorBarDragging = true;
        m_colorBarDragMode = hitTestColorBar(event->pos());
        m_windowDragFloor0 = m_colorFloorFrac;
        m_windowDragCeil0 = m_colorCeilFrac;
        m_windowDragPressFrac = fracFromBarY(event->pos().y());
        applyColorBarDrag(event->pos().y());
        grabMouse();
        event->accept();
        return;
    }
```

`mouseMoveEvent` while dragging:

```cpp
    if (m_colorBarDragging) {
        applyColorBarDrag(event->pos().y());
        event->accept();
        return;
    }
```

Hover (not dragging) when over the bar — replace `setCursor(Qt::SizeVerCursor)` with `setColorBarHoverCursor(event->pos())`.

`mouseReleaseEvent` / `mouseDoubleClickEvent`: also set `m_colorBarDragMode = ColorBarDrag::None`.

- [ ] **Step 3: Build**

```bash
cmake --build build --target radiacode-monitor -j$(nproc)
```

Expected: success, no leftover `applyColorScaleFromBarY` / `m_colorBarDragFloor` / `kColorFloorMax`.

- [ ] **Step 4: Commit**

```bash
git add src/spectrumwaterfall.h src/spectrumwaterfall.cpp
git commit -m "feat: drag colour-bar window from the middle"
```

---

### Task 5: Changelog and verify

**Files:**
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Unreleased note** under `### Changed`:

```markdown
- Waterfall **colour bar** (SDR Console): palette is squeezed between floor and ceil;
  below = black, above = white. Drag edges to resize; drag the middle to slide the
  window. Bar axis is 0…auto-max (not 0…200 %).
```

- [ ] **Step 2: Release build**

```bash
cmake --build build --target radiacode-monitor -j$(nproc)
```

Expected: `[100%] Built target radiacode-monitor`

Manual (when a display is available): default full rainbow; pinch 20–50; slide middle; click black/white; double-click reset; spinbox 200 % (colour to top, no white pad); plot wheel / selection unchanged.

- [ ] **Step 3: Commit**

```bash
git add CHANGELOG.md
git commit -m "docs: note SDR-style colour-bar window"
```

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| Bar axis 0…auto-max | 2 (`fracFromBarY`) + 3 (paint) |
| Black / squeezed / white | 3 (`rateToColor(frac * displayMax)`) |
| Thin edge marks | 3 |
| Ceil > 100 % → no white pad | 3 (`yFromBarFrac` clamps to 1; mapping uses real ceil) |
| Edge / interior / pad hit-test | 4 |
| Window drag, width preserved, clamp | 4 (`setColorWindow` + spanMax) |
| Floor not capped at 50 % | 2 |
| Min gap ~5 % of ceil | 2 (`kColorMinGapFrac`) |
| Spinbox / `colorScaleChanged` | unchanged (ceil setter still emits) |
| Reset double-click / menu | already calls `resetColorScale` |
| No PNG / wheel / new widgets | non-goals, no tasks |
