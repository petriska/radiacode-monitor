#include "spectrumwaterfall.h"
#include "spectrogramcompress.h"
#include "spectrogramfile.h"
#include "spectrogramrecorder.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// GQRX / SDR# waterfall (sampled from Wikimedia PAL-I.png colour bar).
// t=0 min (black) … t=1 max (dark red); cyan→white→yellow, no green.
struct HeatStop {
    float t;
    int r;
    int g;
    int b;
};

constexpr HeatStop kHeatStops[] = {
    {0.00f, 0, 0, 0},
    {0.03f, 0, 0, 20},
    {0.08f, 0, 0, 50},
    {0.16f, 0, 0, 80},
    {0.22f, 1, 0, 128},
    {0.25f, 1, 16, 157},
    {0.28f, 13, 66, 194},
    {0.31f, 22, 123, 237},
    {0.34f, 40, 149, 254},
    {0.37f, 79, 170, 253},
    {0.41f, 157, 208, 255},
    {0.44f, 239, 250, 255},
    {0.47f, 255, 254, 223},
    {0.50f, 255, 255, 175},
    {0.53f, 255, 255, 77},
    {0.56f, 252, 248, 1},
    {0.59f, 255, 222, 5},
    {0.63f, 255, 193, 12},
    {0.66f, 255, 137, 19},
    {0.69f, 254, 90, 19},
    {0.72f, 253, 50, 9},
    {0.75f, 255, 13, 2},
    {0.81f, 219, 1, 0},
    {0.88f, 154, 0, 0},
    {1.00f, 76, 0, 2},
};

QRgb heatMapColor(float u)
{
    constexpr int n = int(sizeof(kHeatStops) / sizeof(kHeatStops[0]));
    if (u <= kHeatStops[0].t) {
        return qRgb(kHeatStops[0].r, kHeatStops[0].g, kHeatStops[0].b);
    }
    if (u >= kHeatStops[n - 1].t) {
        return qRgb(kHeatStops[n - 1].r, kHeatStops[n - 1].g, kHeatStops[n - 1].b);
    }
    int i = 1;
    while (i < n && u > kHeatStops[i].t) {
        ++i;
    }
    const HeatStop &a = kHeatStops[i - 1];
    const HeatStop &c = kHeatStops[i];
    const float span = c.t - a.t;
    const float s = (span > 1e-8f) ? (u - a.t) / span : 0.0f;
    const int r = int(std::lround(a.r + (c.r - a.r) * double(s)));
    const int g = int(std::lround(a.g + (c.g - a.g) * double(s)));
    const int b = int(std::lround(a.b + (c.b - a.b) * double(s)));
    return qRgb(r, g, b);
}

} // namespace

SpectrumWaterfall::SpectrumWaterfall(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
    setCursor(Qt::CrossCursor);
    recomputeCapacity();
    setToolTip(tr(
        "Spectrogram / waterfall: count rate per channel over time (ΔN / Δt).\n"
        "Drag left button: select a rectangle (energy × time) for analysis.\n"
        "Drag inside selection: move the box · drag edges/corners: resize.\n"
        "Esc / click outside / right-click → Clear selection.\n"
        "Mouse wheel: scroll history · Double-click: jump to live.\n"
        "Colour bar (right): drag top = sensitivity (ceil), bottom = floor;\n"
        "wheel over bar = scale; double-click bar = reset to auto 0…max.\n"
        "Right-click menu: Follow live, Go to oldest, PNG, Save/Load history.\n"
        "Time is 1:1 (one row = one pixel)."));
}

void SpectrumWaterfall::recomputeCapacity()
{
    // Each display row covers ~integrate seconds (1 s poll cadence).
    const int secPerRow = qMax(1, m_integrate);
    const int rows = int((qint64(m_historyMinutes) * 60) / secPerRow);
    m_maxRows = qBound(kMinRows, rows, kMaxRowsCap);
    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
    }
    clampScroll();
}

void SpectrumWaterfall::setHistoryMinutes(int minutes)
{
    const int m = qBound(kMinHistoryMinutes, minutes, kMaxHistoryMinutes);
    if (m_historyMinutes == m) {
        return;
    }
    m_historyMinutes = m;
    recomputeCapacity();
    rebuildViewportImage();
    emitScrollSignals();
    update();
}

void SpectrumWaterfall::setIntegrateCount(int n)
{
    const int k = qBound(1, n, kMaxIntegrate);
    if (m_integrate == k) {
        return;
    }
    m_integrate = k;
    m_integrateProgress = 0;
    recomputeCapacity();
    rebuildViewportImage();
    emitScrollSignals();
    update();
}

void SpectrumWaterfall::setScrollFromNewest(int rows)
{
    const int before = m_scrollFromNewest;
    m_scrollFromNewest = rows;
    clampScroll();
    if (m_scrollFromNewest == before) {
        return;
    }
    rebuildViewportImage();
    emitScrollSignals();
    if ((before == 0) != (m_scrollFromNewest == 0)) {
        emit followLiveChanged(m_scrollFromNewest == 0);
    }
    update();
}

void SpectrumWaterfall::followLive()
{
    setScrollFromNewest(0);
}

void SpectrumWaterfall::goToOldest()
{
    setScrollFromNewest(maxScroll());
}

void SpectrumWaterfall::setDeviceSerial(const QString &serial)
{
    m_deviceSerial = serial.trimmed();
}

void SpectrumWaterfall::setRecorder(SpectrogramRecorder *recorder)
{
    m_recorder = recorder;
}

void SpectrumWaterfall::clampScroll()
{
    m_scrollFromNewest = qBound(0, m_scrollFromNewest, maxScroll());
}

int SpectrumWaterfall::maxScroll() const
{
    const QRect imgRect = plotRect().adjusted(1, 1, -1, -1);
    const int plotH = qMax(1, imgRect.height());
    const int nRows = m_rows.size();
    const int visible = qMin(nRows, plotH);
    return qMax(0, nRows - visible);
}

void SpectrumWaterfall::emitScrollSignals()
{
    emit scrollChanged(m_scrollFromNewest, maxScroll());
}

void SpectrumWaterfall::setViewRange(double xMin, double xMax)
{
    if (xMax < xMin) {
        std::swap(xMin, xMax);
    }
    if (qFuzzyCompare(m_xMin, xMin) && qFuzzyCompare(m_xMax, xMax)) {
        return;
    }
    m_xMin = xMin;
    m_xMax = xMax;
    update();
}

void SpectrumWaterfall::setCalibration(float a0, float a1, float a2)
{
    m_a0 = a0;
    m_a1 = a1;
    m_a2 = a2;
    update();
}

bool SpectrumWaterfall::hasEnergyCalibration() const
{
    return hasEnergyAxis();
}

void SpectrumWaterfall::clear()
{
    m_rows.clear();
    m_channels = 0;
    m_displayMax = 1.0f;
    m_image = QImage();
    m_viewportFirstRow = -1;
    m_viewportCount = 0;
    m_hasBaseline = false;
    m_baseline = Snapshot{};
    m_integrateProgress = 0;
    const bool wasFollow = (m_scrollFromNewest == 0);
    m_scrollFromNewest = 0;
    m_linkedCh = -1;
    m_selectDragging = false;
    m_selectMoving = false;
    m_selectResizing = false;
    m_selectDragMoved = false;
    m_resizeEdges = 0;
    clearSelection();
    clearCursor();
    if (!wasFollow) {
        emit followLiveChanged(true);
    }
    emitScrollSignals();
    update();
}

void SpectrumWaterfall::clearSelection()
{
    if (!m_selection.valid && !m_selectDragging && !m_selectMoving && !m_selectResizing) {
        return;
    }
    m_selection = Selection{};
    m_selectDragging = false;
    m_selectMoving = false;
    m_selectResizing = false;
    m_selectDragMoved = false;
    m_resizeEdges = 0;
    unsetCursor();
    emitSelectionChanged();
    update();
}

SpectrumWaterfall::SelectionSpectrum SpectrumWaterfall::extractSelectionSpectrum() const
{
    SelectionSpectrum out;
    out.a0 = m_a0;
    out.a1 = m_a1;
    out.a2 = m_a2;
    if (!m_selection.valid || m_channels <= 0 || m_rows.isEmpty()) {
        return out;
    }
    const int r0 = qBound(0, m_selection.row0, m_rows.size() - 1);
    const int r1 = qBound(r0, m_selection.row1, m_rows.size() - 1);

    // Full energy range over the selected time window (gray + blue highlight in the dialog).
    out.counts = QVector<quint32>(m_channels, 0);
    quint64 liveSum = 0;
    for (int r = r0; r <= r1; ++r) {
        const Row &row = m_rows.at(r);
        liveSum += row.intervalSec;
        const int n = qMin(m_channels, row.deltas.size());
        for (int ch = 0; ch < n; ++ch) {
            const quint64 v = quint64(out.counts[ch]) + quint64(row.deltas[ch]);
            out.counts[ch] = v > 0xffffffffu ? 0xffffffffu : quint32(v);
        }
    }
    out.durationSec = liveSum > 0xffffffffu ? 0xffffffffu : quint32(liveSum);
    return out;
}

QVector<SpectrumWaterfall::SelectionMcsPoint> SpectrumWaterfall::extractSelectionMcs() const
{
    QVector<SelectionMcsPoint> out;
    if (!m_selection.valid || m_channels <= 0 || m_rows.isEmpty()) {
        return out;
    }
    const int r0 = qBound(0, m_selection.row0, m_rows.size() - 1);
    const int r1 = qBound(r0, m_selection.row1, m_rows.size() - 1);
    const int c0 = qBound(0, m_selection.ch0, m_channels);
    const int c1 = qBound(c0, m_selection.ch1, m_channels);

    out.reserve(r1 - r0 + 1);
    for (int r = r0; r <= r1; ++r) {
        const Row &row = m_rows.at(r);
        SelectionMcsPoint p;
        p.wallTime = row.wallTime;
        p.liveTimeSec = row.liveTimeSec;
        p.intervalSec = row.intervalSec;
        // Sum ΔN first, then convert to cps from the row interval so full and
        // selection series stay consistent (and NaN rates cannot drop a sample).
        quint64 fullCounts = 0;
        quint64 selCounts = 0;
        const int nD = qMin(m_channels, row.deltas.size());
        for (int ch = 0; ch < nD; ++ch) {
            fullCounts += row.deltas[ch];
            if (ch >= c0 && ch < c1) {
                selCounts += row.deltas[ch];
            }
        }
        // Net mode: rates already bake BG subtraction per channel; recompute
        // cps from displayRate so MCS matches the waterfall view mode.
        double fullCps = 0;
        double selCps = 0;
        if (netModeActive()) {
            const int nR = qMin(m_channels, row.rates.size());
            for (int ch = 0; ch < nR; ++ch) {
                const double rate = double(displayRate(row.rates[ch], ch));
                if (!std::isfinite(rate)) {
                    continue;
                }
                fullCps += rate;
                if (ch >= c0 && ch < c1) {
                    selCps += rate;
                }
            }
        } else if (row.intervalSec > 0) {
            fullCps = double(fullCounts) / double(row.intervalSec);
            selCps = double(selCounts) / double(row.intervalSec);
        }
        p.fullCps = fullCps;
        p.fullCounts = fullCounts;
        p.cps = selCps;
        p.counts = selCounts;
        out.append(p);
    }
    return out;
}

void SpectrumWaterfall::emitSelectionChanged()
{
    if (m_selection.valid) {
        emit selectionChanged(true, m_selection.ch0, m_selection.ch1, m_selection.row0,
                              m_selection.row1);
    } else {
        emit selectionChanged(false, 0, 0, 0, 0);
    }
}

void SpectrumWaterfall::adjustSelectionAfterHistoryTrim(int dropped)
{
    if (!m_selection.valid || dropped <= 0) {
        return;
    }
    m_selection.row0 -= dropped;
    m_selection.row1 -= dropped;
    if (m_selection.row1 < 0) {
        m_selection = Selection{};
        emitSelectionChanged();
        return;
    }
    m_selection.row0 = qMax(0, m_selection.row0);
    emitSelectionChanged();
}

int SpectrumWaterfall::rowFromWidgetY(int y, const TimeView &tv) const
{
    if (tv.visible <= 0) {
        return 0;
    }
    const int local = std::clamp(y - tv.dest.top(), 0, tv.visible - 1);
    return tv.firstRow + local;
}

bool SpectrumWaterfall::selectionContainsWidgetPos(const QPoint &pos) const
{
    if (!m_selection.valid) {
        return false;
    }
    TimeView tv;
    if (!timeView(plotRect(), &tv)) {
        return false;
    }
    const QRect r = selectionPixelRect(tv);
    return r.contains(pos);
}

void SpectrumWaterfall::setSelectionFromCorners(const QPoint &a, const QPoint &b, bool notify)
{
    const QRect plot = plotRect();
    TimeView tv;
    if (!timeView(plot, &tv) || m_channels <= 0) {
        return;
    }

    auto clampPos = [&](QPoint p) {
        p.setX(qBound(tv.dest.left(), p.x(), tv.dest.right()));
        p.setY(qBound(tv.dest.top(), p.y(), tv.dest.bottom()));
        return p;
    };
    const QPoint pa = clampPos(a);
    const QPoint pb = clampPos(b);

    int chA = int(std::floor(channelAtPlotX(pa.x(), plot)));
    int chB = int(std::floor(channelAtPlotX(pb.x(), plot)));
    chA = std::clamp(chA, 0, m_channels - 1);
    chB = std::clamp(chB, 0, m_channels - 1);

    const int rowA = rowFromWidgetY(pa.y(), tv);
    const int rowB = rowFromWidgetY(pb.y(), tv);

    Selection s;
    s.ch0 = qMin(chA, chB);
    s.ch1 = qMax(chA, chB) + 1; // exclusive
    s.row0 = qMin(rowA, rowB);
    s.row1 = qMax(rowA, rowB);
    s.valid = (s.ch1 > s.ch0) && (s.row1 >= s.row0);
    m_selection = s;
    if (notify) {
        emitSelectionChanged();
    }
}

void SpectrumWaterfall::moveSelectionBy(int dCh, int dRow, bool notify)
{
    if (!m_moveOrig.valid || m_channels <= 0 || m_rows.isEmpty()) {
        return;
    }
    const int w = m_moveOrig.ch1 - m_moveOrig.ch0; // exclusive width
    const int h = m_moveOrig.row1 - m_moveOrig.row0; // inclusive span
    if (w <= 0) {
        return;
    }

    int ch0 = m_moveOrig.ch0 + dCh;
    int row0 = m_moveOrig.row0 + dRow;
    ch0 = qBound(0, ch0, m_channels - w);
    row0 = qBound(0, row0, m_rows.size() - 1 - h);

    m_selection.ch0 = ch0;
    m_selection.ch1 = ch0 + w;
    m_selection.row0 = row0;
    m_selection.row1 = row0 + h;
    m_selection.valid = true;
    if (notify) {
        emitSelectionChanged();
    }
}

int SpectrumWaterfall::hitTestSelectionEdge(const QPoint &pos) const
{
    if (!m_selection.valid) {
        return 0;
    }
    TimeView tv;
    if (!timeView(plotRect(), &tv)) {
        return 0;
    }
    const QRect r = selectionPixelRect(tv);
    if (r.isEmpty()) {
        return 0;
    }

    const int k = kSelectEdgeHitPx;
    const QRect outer = r.adjusted(-k, -k, k, k);
    if (!outer.contains(pos)) {
        return 0;
    }

    int edges = 0;
    if (pos.x() <= r.left() + k) {
        edges |= kEdgeLeft;
    }
    if (pos.x() >= r.right() - k) {
        edges |= kEdgeRight;
    }
    if (pos.y() <= r.top() + k) {
        edges |= kEdgeTop;
    }
    if (pos.y() >= r.bottom() - k) {
        edges |= kEdgeBottom;
    }

    // Only count an edge if the point is roughly along that side (not far outside opposite).
    if (pos.y() < r.top() - k || pos.y() > r.bottom() + k) {
        edges &= ~(kEdgeLeft | kEdgeRight);
        // keep top/bottom if in vertical band of outer
    }
    if (pos.x() < r.left() - k || pos.x() > r.right() + k) {
        edges &= ~(kEdgeTop | kEdgeBottom);
    }

    // Interior (not near any edge) → 0 (caller treats as move).
    const QRect inner = r.adjusted(k, k, -k, -k);
    if (inner.isValid() && inner.contains(pos)) {
        return 0;
    }
    return edges;
}

void SpectrumWaterfall::applySelectionHoverCursor(const QPoint &pos)
{
    if (!m_selection.valid) {
        if (cursor().shape() != Qt::ArrowCursor && cursor().shape() != Qt::CrossCursor) {
            unsetCursor();
        }
        return;
    }
    const int edges = hitTestSelectionEdge(pos);
    if (edges != 0) {
        const bool hor = (edges & (kEdgeLeft | kEdgeRight)) != 0;
        const bool ver = (edges & (kEdgeTop | kEdgeBottom)) != 0;
        if (hor && ver) {
            // Diagonal: TL-BR vs TR-BL
            const bool tlbr = ((edges & kEdgeLeft) && (edges & kEdgeTop))
                || ((edges & kEdgeRight) && (edges & kEdgeBottom));
            setCursor(tlbr ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        } else if (hor) {
            setCursor(Qt::SizeHorCursor);
        } else {
            setCursor(Qt::SizeVerCursor);
        }
        return;
    }
    if (selectionContainsWidgetPos(pos)) {
        setCursor(Qt::SizeAllCursor);
    } else {
        unsetCursor();
    }
}

void SpectrumWaterfall::resizeSelectionEdge(int edges, const QPoint &pos, bool notify)
{
    if (!m_moveOrig.valid || edges == 0 || m_channels <= 0 || m_rows.isEmpty()) {
        return;
    }
    const QRect plot = plotRect();
    TimeView tv;
    if (!timeView(plot, &tv)) {
        return;
    }

    auto clampPos = [&](QPoint p) {
        p.setX(qBound(tv.dest.left(), p.x(), tv.dest.right()));
        p.setY(qBound(tv.dest.top(), p.y(), tv.dest.bottom()));
        return p;
    };
    const QPoint p = clampPos(pos);
    int ch = int(std::floor(channelAtPlotX(p.x(), plot)));
    ch = std::clamp(ch, 0, m_channels - 1);
    const int row = rowFromWidgetY(p.y(), tv);

    Selection s = m_moveOrig;
    if (edges & kEdgeLeft) {
        s.ch0 = qBound(0, ch, s.ch1 - 1);
    }
    if (edges & kEdgeRight) {
        // exclusive end: channel under cursor becomes last included → ch1 = ch+1
        s.ch1 = qBound(s.ch0 + 1, ch + 1, m_channels);
    }
    if (edges & kEdgeTop) {
        s.row0 = qBound(0, row, s.row1);
    }
    if (edges & kEdgeBottom) {
        s.row1 = qBound(s.row0, row, m_rows.size() - 1);
    }
    s.valid = (s.ch1 > s.ch0) && (s.row1 >= s.row0);
    m_selection = s;
    if (notify) {
        emitSelectionChanged();
    }
}

void SpectrumWaterfall::setBackground(const QVector<quint32> &counts, quint32 durationSec)
{
    m_bgCounts = counts;
    m_bgDurationSec = durationSec;
    m_displayMax = matrixMaxRate();
    rebuildViewportImage();
    update();
}

void SpectrumWaterfall::clearBackground()
{
    m_bgCounts.clear();
    m_bgDurationSec = 0;
    m_displayMax = matrixMaxRate();
    rebuildViewportImage();
    update();
}

void SpectrumWaterfall::setDisplayMode(DisplayMode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    m_displayMax = matrixMaxRate();
    rebuildViewportImage();
    update();
}

bool SpectrumWaterfall::netModeActive() const
{
    return m_mode == DisplayMode::Net && !m_bgCounts.isEmpty() && m_bgDurationSec > 0;
}

float SpectrumWaterfall::bgRate(int ch) const
{
    if (m_bgDurationSec == 0 || ch < 0 || ch >= m_bgCounts.size()) {
        return 0.0f;
    }
    return float(m_bgCounts.at(ch)) / float(m_bgDurationSec);
}

float SpectrumWaterfall::displayRate(float liveRate, int ch) const
{
    if (!netModeActive()) {
        return liveRate;
    }
    return std::max(0.0f, liveRate - bgRate(ch));
}

void SpectrumWaterfall::setLinkedChannel(int channel)
{
    int ch = channel;
    if (ch >= 0 && (m_channels <= 0 || ch >= m_channels)) {
        ch = -1;
    }
    if (m_linkedCh == ch) {
        return;
    }
    m_linkedCh = ch;
    update();
}

bool SpectrumWaterfall::hasEnergyAxis() const
{
    return std::fabs(static_cast<double>(m_a1)) > 1e-12
        || std::fabs(static_cast<double>(m_a2)) > 1e-12;
}

double SpectrumWaterfall::channelToEnergy(double channel) const
{
    const double ch = channel;
    return static_cast<double>(m_a0) + static_cast<double>(m_a1) * ch
        + static_cast<double>(m_a2) * ch * ch;
}

QRect SpectrumWaterfall::plotRect() const
{
    return QRect(kMarginLeft,
                 kMarginTop,
                 std::max(1, width() - kMarginLeft - kMarginRight),
                 std::max(1, height() - kMarginTop - kMarginBottom));
}

float SpectrumWaterfall::colorMapMin() const
{
    return std::max(0.0f, m_displayMax * m_colorFloorFrac);
}

float SpectrumWaterfall::colorMapMax() const
{
    return std::max(1e-9f, m_displayMax * m_colorCeilFrac);
}

void SpectrumWaterfall::setColorCeilFraction(float frac)
{
    const float f = std::clamp(frac, kColorCeilMin, kColorCeilMax);
    // Keep floor strictly below ceil.
    float floor = m_colorFloorFrac;
    const float maxFloor = f * (1.0f - kColorMinGapFrac);
    if (floor > maxFloor) {
        floor = std::max(0.0f, maxFloor);
    }
    if (qAbs(f - m_colorCeilFrac) < 1e-6f && qAbs(floor - m_colorFloorFrac) < 1e-6f) {
        return;
    }
    m_colorCeilFrac = f;
    m_colorFloorFrac = floor;
    rebuildViewportImage();
    update();
    emit colorScaleChanged(m_colorFloorFrac, m_colorCeilFrac);
}

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

void SpectrumWaterfall::resetColorScale()
{
    if (qAbs(m_colorCeilFrac - 1.0f) < 1e-6f && m_colorFloorFrac < 1e-6f) {
        return;
    }
    m_colorCeilFrac = 1.0f;
    m_colorFloorFrac = 0.0f;
    rebuildViewportImage();
    update();
    emit colorScaleChanged(m_colorFloorFrac, m_colorCeilFrac);
}

void SpectrumWaterfall::setColorMap(ColorMap map)
{
    if (m_colorMap == map) {
        return;
    }
    m_colorMap = map;
    rebuildViewportImage();
    update();
}

QRect SpectrumWaterfall::colorBarRect() const
{
    const QRect plot = plotRect();
    return QRect(plot.right() + kColorBarGap, plot.top(), kColorBarWidth, plot.height());
}

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

QRgb SpectrumWaterfall::rateToColor(float rate) const
{
    const float lo = colorMapMin();
    const float hi = colorMapMax();
    if (hi <= lo + 1e-12f) {
        return qRgb(0, 0, 0);
    }
    float t = 0.0f;
    if (m_colorMap == ColorMap::Log) {
        // log(rate+ε) so zero rates stay defined; ε ~ 4 decades below ceil.
        const float eps = std::max(hi * 1e-4f, 1e-12f);
        const float den = std::log(hi + eps) - std::log(lo + eps);
        if (den <= 1e-12f) {
            return qRgb(0, 0, 0);
        }
        t = (std::log(std::max(rate, 0.0f) + eps) - std::log(lo + eps)) / den;
    } else {
        t = (rate - lo) / (hi - lo);
    }
    if (t < 0.0f) {
        return qRgb(0, 0, 0);
    }
    return heatMapColor(std::min(t, 1.0f));
}

void SpectrumWaterfall::drawColorBar(QPainter &p, const QRect &plot) const
{
    Q_UNUSED(plot);
    const QRect bar = colorBarRect();
    if (bar.height() < 4) {
        return;
    }

    // Each row is the colour of that fraction of auto-max (matches spectrogram).
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

    // Scale % of auto max (ceil).
    const QFontMetrics fm = p.fontMetrics();
    p.setPen(QColor(170, 172, 180));
    const QString pct = QStringLiteral("%1%").arg(int(std::lround(double(m_colorCeilFrac) * 100.0)));
    p.drawText(bar.left() + (bar.width() - fm.horizontalAdvance(pct)) / 2,
               bar.top() - 2, pct);
}

bool SpectrumWaterfall::makeRow(const Snapshot &prev, const Snapshot &cur, Row *out) const
{
    if (!out || m_channels <= 0) {
        return false;
    }
    const qint64 dT = qint64(cur.durationSec) - qint64(prev.durationSec);
    if (dT <= 0) {
        return false;
    }

    const int n = qMin(m_channels, qMin(prev.counts.size(), cur.counts.size()));
    out->rates.resize(m_channels);
    out->deltas.resize(m_channels);
    out->liveTimeSec = cur.durationSec;
    out->intervalSec = static_cast<quint32>(dT);
    out->wallTime = cur.wallTime;

    for (int ch = 0; ch < m_channels; ++ch) {
        quint32 dN = 0;
        if (ch < n) {
            const qint64 d = qint64(cur.counts.at(ch)) - qint64(prev.counts.at(ch));
            dN = d > 0 ? static_cast<quint32>(d) : 0u;
        }
        out->deltas[ch] = dN;
        out->rates[ch] = float(dN) / float(dT);
    }
    return true;
}

float SpectrumWaterfall::matrixMaxRate() const
{
    float mx = 0.0f;
    for (const Row &row : m_rows) {
        const int n = row.rates.size();
        for (int ch = 0; ch < n; ++ch) {
            mx = std::max(mx, displayRate(row.rates[ch], ch));
        }
    }
    if (mx < 1e-6f) {
        mx = 1e-6f;
    }
    return mx;
}

void SpectrumWaterfall::ensureViewportImage(int visibleRows)
{
    if (m_channels <= 0 || visibleRows <= 0) {
        m_image = QImage();
        m_viewportFirstRow = -1;
        m_viewportCount = 0;
        return;
    }
    if (m_image.width() == m_channels && m_image.height() == visibleRows
        && m_image.format() == QImage::Format_RGB32) {
        return;
    }
    m_image = QImage(m_channels, visibleRows, QImage::Format_RGB32);
    m_image.fill(QColor(20, 22, 26));
    m_viewportFirstRow = -1;
    m_viewportCount = 0;
}

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
        const int rowIdx = tv.firstRow + i;
        const Row &row = m_rows.at(rowIdx);
        auto *line = reinterpret_cast<QRgb *>(m_image.scanLine(i));
        const int n = qMin(m_channels, row.rates.size());
        for (int x = 0; x < m_channels; ++x) {
            const float live = (x < n) ? row.rates[x] : 0.0f;
            line[x] = rateToColor(displayRate(live, x));
        }
    }
    m_viewportFirstRow = tv.firstRow;
    m_viewportCount = tv.visible;
}

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
    return true;
}

void SpectrumWaterfall::refreshCursorAfterScroll(bool droppedOldest)
{
    if (m_cursorRow < 0) {
        return;
    }
    if (droppedOldest) {
        m_cursorRow -= 1;
    }
    if (m_cursorRow < 0 || m_cursorRow >= m_rows.size()) {
        clearCursor();
        return;
    }
    if (m_cursorCh < 0 || m_cursorCh >= m_channels) {
        return;
    }
    const Row &r = m_rows[m_cursorRow];
    const float live = (m_cursorCh < r.rates.size()) ? r.rates[m_cursorCh] : 0.0f;
    const float rate = displayRate(live, m_cursorCh);
    const quint32 dN = (m_cursorCh < r.deltas.size()) ? r.deltas[m_cursorCh] : 0u;
    const double e = channelToEnergy(double(m_cursorCh));
    emit cursorInfoChanged(m_cursorCh, e, rate, dN, r.liveTimeSec,
                           ageFromNewestSec(m_cursorRow));
}

void SpectrumWaterfall::appendDisplayRow(Row &&row)
{
    const bool wasFollow = (m_scrollFromNewest == 0);
    const bool droppedOldest = (m_rows.size() >= m_maxRows);

    // Continuous disk recording (C1) — same row model as .rcsg save/load.
    if (m_recorder && m_recorder->isRecording()) {
        SpectrogramFile::Row out;
        out.rates = row.rates;
        out.deltas = row.deltas;
        out.liveTimeSec = row.liveTimeSec;
        out.intervalSec = row.intervalSec;
        out.wallTime = row.wallTime;
        m_recorder->appendRow(out, nullptr);
    }

    m_rows.append(std::move(row));
    int dropped = 0;
    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
        ++dropped;
    }
    if (dropped > 0) {
        adjustSelectionAfterHistoryTrim(dropped);
    }

    // Keep the same absolute window when the user has scrolled into history.
    if (!wasFollow) {
        m_scrollFromNewest += 1;
    }
    clampScroll();

    const float newMax = matrixMaxRate();
    const float oldMax = m_displayMax;
    const bool scaleChanged =
        (oldMax < 1e-6f)
        || (newMax > oldMax * (1.0f + kScaleHysteresis))
        || (newMax < oldMax * (1.0f - kScaleHysteresis));

    if (scaleChanged || !wasFollow) {
        m_displayMax = newMax;
        rebuildViewportImage();
        refreshCursorAfterScroll(droppedOldest);
        emitScrollSignals();
        update();
        return;
    }

    // Live follow + stable scale: scroll viewport image + paint new bottom line.
    m_displayMax = newMax;
    TimeView tv;
    if (!timeView(plotRect(), &tv)) {
        rebuildViewportImage();
        refreshCursorAfterScroll(droppedOldest);
        emitScrollSignals();
        update();
        return;
    }

    // Incremental only when the cache already matches this viewport height and
    // was showing the previous live window (firstRow advanced by 1).
    const bool liveScrollOk =
        !m_image.isNull()
        && m_image.width() == m_channels
        && m_image.height() == tv.visible
        && m_viewportCount == tv.visible
        && m_viewportFirstRow == tv.firstRow - 1;

    if (!liveScrollOk) {
        rebuildViewportImage();
    } else {
        const int h = m_image.height();
        const int w = m_image.width();
        if (h >= 2) {
            const int bpl = m_image.bytesPerLine();
            uchar *bits = m_image.bits();
            std::memmove(bits, bits + bpl, size_t(bpl) * size_t(h - 1));
        }
        const Row &newest = m_rows.last();
        auto *line = reinterpret_cast<QRgb *>(m_image.scanLine(h - 1));
        const int n = qMin(w, newest.rates.size());
        for (int x = 0; x < w; ++x) {
            const float live = (x < n) ? newest.rates[x] : 0.0f;
            line[x] = rateToColor(displayRate(live, x));
        }
        m_viewportFirstRow = tv.firstRow;
        m_viewportCount = tv.visible;
    }

    refreshCursorAfterScroll(droppedOldest);
    emitScrollSignals();
    update();
}

void SpectrumWaterfall::pushSpectrum(const QVector<quint32> &counts, quint32 durationSec)
{
    if (counts.isEmpty()) {
        return;
    }

    const int n = counts.size();
    if (n != m_channels) {
        m_rows.clear();
        m_channels = n;
        m_hasBaseline = false;
        m_baseline = Snapshot{};
        m_integrateProgress = 0;
        m_displayMax = 1.0f;
        m_image = QImage();
        m_viewportFirstRow = -1;
        m_viewportCount = 0;
        const bool wasFollow = (m_scrollFromNewest == 0);
        m_scrollFromNewest = 0;
        clearCursor();
        if (m_xMax <= m_xMin || m_xMax > n) {
            m_xMin = 0;
            m_xMax = n;
        }
        if (!wasFollow) {
            emit followLiveChanged(true);
        }
    }

    Snapshot cur;
    cur.counts = counts;
    cur.durationSec = durationSec;
    cur.wallTime = QDateTime::currentDateTime();

    if (!m_hasBaseline) {
        m_baseline = std::move(cur);
        m_hasBaseline = true;
        m_integrateProgress = 0;
        update();
        return;
    }

    ++m_integrateProgress;
    if (m_integrateProgress < m_integrate) {
        // Still accumulating polls toward one display row; keep baseline fixed.
        return;
    }
    m_integrateProgress = 0;

    Row row;
    if (!makeRow(m_baseline, cur, &row)) {
        // Spectrum reset or live-time did not advance — resync baseline.
        m_baseline = std::move(cur);
        return;
    }

    m_baseline = std::move(cur);
    appendDisplayRow(std::move(row));
}

int SpectrumWaterfall::ageFromNewestSec(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= m_rows.size()) {
        return 0;
    }
    int age = 0;
    for (int i = rowIndex + 1; i < m_rows.size(); ++i) {
        age += int(m_rows[i].intervalSec);
    }
    return age;
}

double SpectrumWaterfall::channelAtPlotX(int x, const QRect &plot) const
{
    const double x0 = std::clamp(m_xMin, 0.0, double(std::max(1, m_channels)));
    const double x1 = std::clamp(m_xMax, 0.0, double(std::max(1, m_channels)));
    if (plot.width() <= 1 || x1 <= x0) {
        return x0;
    }
    const double t = (double(x) - plot.left()) / double(plot.width() - 1);
    return x0 + t * (x1 - x0);
}

int SpectrumWaterfall::rowAtPlotY(int y, const QRect &plot) const
{
    TimeView tv;
    if (!timeView(plot, &tv)) {
        return -1;
    }
    if (y < tv.dest.top() || y > tv.dest.bottom()) {
        return -1;
    }
    const int local = y - tv.dest.top();
    if (local < 0 || local >= tv.visible) {
        return -1;
    }
    return tv.firstRow + local;
}

void SpectrumWaterfall::clearCursor()
{
    if (m_cursorCh < 0 && m_cursorRow < 0) {
        return;
    }
    m_cursorCh = -1;
    m_cursorRow = -1;
    emit cursorInfoChanged(-1, 0.0, 0.0f, 0, 0, 0);
    update();
}

void SpectrumWaterfall::setCursorFromPos(const QPoint &pos)
{
    const QRect plot = plotRect();
    if (m_rows.isEmpty() || m_channels <= 0 || !plot.contains(pos)) {
        clearCursor();
        return;
    }

    const int row = rowAtPlotY(pos.y(), plot);
    if (row < 0) {
        clearCursor();
        return;
    }

    const double chF = channelAtPlotX(pos.x(), plot);
    int ch = static_cast<int>(std::floor(chF));
    ch = std::clamp(ch, 0, m_channels - 1);

    if (ch == m_cursorCh && row == m_cursorRow) {
        return;
    }
    m_cursorCh = ch;
    m_cursorRow = row;

    const Row &r = m_rows[row];
    const float live = (ch < r.rates.size()) ? r.rates[ch] : 0.0f;
    const float rate = displayRate(live, ch);
    const quint32 dN = (ch < r.deltas.size()) ? r.deltas[ch] : 0u;
    const double e = channelToEnergy(double(ch));
    emit cursorInfoChanged(ch, e, rate, dN, r.liveTimeSec, ageFromNewestSec(row));
    update();
}

void SpectrumWaterfall::mousePressEvent(QMouseEvent *event)
{
    // SDR-style colour bar (right of plot): adjust map floor/ceil.
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

    if (event->button() == Qt::LeftButton && plotRect().contains(event->pos())
        && !m_rows.isEmpty()) {
        m_selectDragMoved = false;
        m_selectPressPos = event->pos();
        m_selectCurrPos = event->pos();
        m_selectMoving = false;
        m_selectResizing = false;
        m_selectDragging = false;
        m_resizeEdges = 0;
        setFocus(Qt::MouseFocusReason);

        // Edge/corner of existing selection → resize that side(s).
        if (m_selection.valid) {
            const int edges = hitTestSelectionEdge(event->pos());
            if (edges != 0) {
                m_selectResizing = true;
                m_resizeEdges = edges;
                m_moveOrig = m_selection;
                applySelectionHoverCursor(event->pos());
                grabMouse();
                event->accept();
                return;
            }
            // Interior → move mode (fixed size).
            if (selectionContainsWidgetPos(event->pos())) {
                m_selectMoving = true;
                m_moveOrig = m_selection;
                const QRect plot = plotRect();
                TimeView tv;
                timeView(plot, &tv);
                m_movePressCh =
                    std::clamp(int(std::floor(channelAtPlotX(event->pos().x(), plot))), 0,
                               m_channels - 1);
                m_movePressRow = rowFromWidgetY(event->pos().y(), tv);
                setCursor(Qt::SizeAllCursor);
                grabMouse();
                event->accept();
                return;
            }
        }

        // Otherwise create a new rectangle.
        m_selectDragging = true;
        grabMouse();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SpectrumWaterfall::mouseMoveEvent(QMouseEvent *event)
{
    if (m_colorBarDragging) {
        applyColorBarDrag(event->pos().y());
        event->accept();
        return;
    }

    if (m_selectResizing) {
        m_selectCurrPos = event->pos();
        if ((event->pos() - m_selectPressPos).manhattanLength() >= kSelectDragThresholdPx
            || m_selectDragMoved) {
            m_selectDragMoved = true;
            resizeSelectionEdge(m_resizeEdges, event->pos(), false);
            repaint();
        }
        event->accept();
        return;
    }

    if (m_selectMoving) {
        m_selectCurrPos = event->pos();
        const QRect plot = plotRect();
        TimeView tv;
        if (timeView(plot, &tv)) {
            const int ch = std::clamp(int(std::floor(channelAtPlotX(event->pos().x(), plot))),
                                      0, m_channels - 1);
            const int row = rowFromWidgetY(event->pos().y(), tv);
            const int dCh = ch - m_movePressCh;
            const int dRow = row - m_movePressRow;
            if (std::abs(dCh) + std::abs(dRow) > 0
                || (event->pos() - m_selectPressPos).manhattanLength()
                    >= kSelectDragThresholdPx) {
                m_selectDragMoved = true;
                moveSelectionBy(dCh, dRow, false);
                repaint();
            }
        }
        event->accept();
        return;
    }

    if (m_selectDragging) {
        m_selectCurrPos = event->pos();
        const QPoint d = m_selectCurrPos - m_selectPressPos;
        if (d.manhattanLength() >= kSelectDragThresholdPx) {
            m_selectDragMoved = true;
            // Update geometry + caption every move; notify listeners only on release.
            setSelectionFromCorners(m_selectPressPos, m_selectCurrPos, false);
            repaint(); // immediate redraw so caption tracks the rubber-band
        }
        event->accept();
        return;
    }

    if (colorBarRect().contains(event->pos())) {
        setColorBarHoverCursor(event->pos());
        event->accept();
        return;
    }
    applySelectionHoverCursor(event->pos());
    setCursorFromPos(event->pos());
    event->accept();
}

void SpectrumWaterfall::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_colorBarDragging) {
        m_colorBarDragging = false;
        m_colorBarDragMode = ColorBarDrag::None;
        if (mouseGrabber() == this) {
            releaseMouse();
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton
        && (m_selectDragging || m_selectMoving || m_selectResizing)) {
        if (mouseGrabber() == this) {
            releaseMouse();
        }

        if (m_selectResizing) {
            m_selectResizing = false;
            if (m_selectDragMoved) {
                resizeSelectionEdge(m_resizeEdges, event->pos(), true);
            }
            m_resizeEdges = 0;
            m_selectDragMoved = false;
            applySelectionHoverCursor(event->pos());
            update();
            event->accept();
            return;
        }

        if (m_selectMoving) {
            m_selectMoving = false;
            if (m_selectDragMoved) {
                const QRect plot = plotRect();
                TimeView tv;
                if (timeView(plot, &tv)) {
                    const int ch =
                        std::clamp(int(std::floor(channelAtPlotX(event->pos().x(), plot))),
                                   0, m_channels - 1);
                    const int row = rowFromWidgetY(event->pos().y(), tv);
                    moveSelectionBy(ch - m_movePressCh, row - m_movePressRow, true);
                } else {
                    emitSelectionChanged();
                }
            }
            // Click inside without move: keep selection.
            m_selectDragMoved = false;
            applySelectionHoverCursor(event->pos());
            update();
            event->accept();
            return;
        }

        m_selectDragging = false;
        if (m_selectDragMoved) {
            setSelectionFromCorners(m_selectPressPos, event->pos(), true);
            update();
        } else {
            // Click outside selection without drag: clear.
            clearSelection();
        }
        m_selectDragMoved = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SpectrumWaterfall::leaveEvent(QEvent *event)
{
    if (!m_selectDragging && !m_selectMoving && !m_selectResizing) {
        clearCursor();
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

void SpectrumWaterfall::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (mouseGrabber() == this) {
            releaseMouse();
        }
        m_colorBarDragging = false;
        m_colorBarDragMode = ColorBarDrag::None;
        m_selectDragging = false;
        m_selectMoving = false;
        m_selectResizing = false;
        m_selectDragMoved = false;
        m_resizeEdges = 0;
        if (colorBarRect().contains(event->pos())) {
            resetColorScale();
            event->accept();
            return;
        }
        followLive();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SpectrumWaterfall::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        clearSelection();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SpectrumWaterfall::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *followAct = menu.addAction(tr("Follow live"));
    followAct->setEnabled(!isFollowingLive());
    followAct->setToolTip(tr("Jump to the newest spectrogram data."));

    QAction *oldestAct = menu.addAction(tr("Go to oldest"));
    oldestAct->setEnabled(!m_rows.isEmpty() && maxScroll() > 0
                          && m_scrollFromNewest < maxScroll());
    oldestAct->setToolTip(
        tr("Show the oldest data still in the history buffer."));

    QAction *clearSelAct = menu.addAction(tr("Clear selection"));
    clearSelAct->setEnabled(m_selection.valid);
    clearSelAct->setToolTip(tr("Remove the analysis rectangle (Esc)."));

    QAction *resetColorAct = menu.addAction(tr("Reset colour scale"));
    resetColorAct->setToolTip(
        tr("Map colours to full auto range (0 … max rate over history).\n"
           "Same as double-click on the colour bar."));

    menu.addSeparator();

    QAction *specAct = menu.addAction(tr("Spectrum from selection…"));
    specAct->setEnabled(m_selection.valid);
    specAct->setToolTip(
        tr("Open a live-updating spectrum of integrated ΔN for the selection."));

    QAction *mcsAct = menu.addAction(tr("MCS from selection…"));
    mcsAct->setEnabled(m_selection.valid);
    mcsAct->setToolTip(
        tr("Show count rate vs time for the selected energy window."));

    QAction *exportSpecAct = menu.addAction(tr("Export selection spectrum…"));
    exportSpecAct->setEnabled(m_selection.valid);
    exportSpecAct->setToolTip(tr("Save the integrated selection spectrum to a file."));

    QAction *exportMcsAct = menu.addAction(tr("Export selection MCS CSV…"));
    exportMcsAct->setEnabled(m_selection.valid);
    exportMcsAct->setToolTip(tr("Save MCS time series (time, cps, counts) as CSV."));

    menu.addSeparator();

    QAction *pngViewAct = menu.addAction(tr("Export view as PNG…"));
    pngViewAct->setEnabled(!m_rows.isEmpty());
    pngViewAct->setToolTip(
        tr("Rasterize the visible time window (and energy zoom) from rate data\n"
           "to a lossless PNG — not a widget screenshot."));

    QAction *pngFullAct = menu.addAction(tr("Export full history as PNG…"));
    pngFullAct->setEnabled(!m_rows.isEmpty());
    pngFullAct->setToolTip(
        tr("Rasterize the entire history buffer from rate data to a lossless PNG.\n"
           "Height = number of rows in memory; width = all channels.\n"
           "May be large for multi-hour buffers."));

    menu.addSeparator();

    QAction *saveAct = menu.addAction(tr("Save history…"));
    saveAct->setEnabled(!m_rows.isEmpty());
    saveAct->setToolTip(
        tr("Save the full spectrogram buffer as a binary .rcsg file\n"
           "(rates, ΔN, wall-clock timestamps, serial, calibration)."));

    QAction *loadAct = menu.addAction(tr("Load history…"));
    loadAct->setToolTip(
        tr("Load a .rcsg spectrogram file into the buffer (replaces current history)."));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == followAct) {
        followLive();
    } else if (chosen == oldestAct) {
        goToOldest();
    } else if (chosen == clearSelAct) {
        clearSelection();
    } else if (chosen == resetColorAct) {
        resetColorScale();
    } else if (chosen == specAct) {
        emit extractSpectrumRequested();
    } else if (chosen == mcsAct) {
        emit extractMcsRequested();
    } else if (chosen == exportSpecAct) {
        emit exportSelectionSpectrumRequested();
    } else if (chosen == exportMcsAct) {
        emit exportSelectionMcsRequested();
    } else if (chosen == pngViewAct) {
        exportAsPng(PngExportScope::View, window());
    } else if (chosen == pngFullAct) {
        exportAsPng(PngExportScope::FullHistory, window());
    } else if (chosen == saveAct) {
        saveHistory(window());
    } else if (chosen == loadAct) {
        loadHistory(window());
    }
    event->accept();
}

QRect SpectrumWaterfall::selectionPixelRect(const TimeView &tv) const
{
    if (!m_selection.valid || tv.visible <= 0 || m_channels <= 0) {
        return {};
    }
    // Only the part overlapping the current viewport is drawn.
    const int row0 = qMax(m_selection.row0, tv.firstRow);
    const int row1 = qMin(m_selection.row1, tv.lastRow);
    if (row1 < row0) {
        return {};
    }

    const double x0 = std::clamp(m_xMin, 0.0, double(m_channels));
    const double x1 = std::clamp(m_xMax, 0.0, double(m_channels));
    if (x1 <= x0 + 1e-9) {
        return {};
    }

    const int ch0 = qMax(m_selection.ch0, int(std::floor(x0)));
    const int ch1 = qMin(m_selection.ch1, int(std::ceil(x1)));
    if (ch1 <= ch0) {
        return {};
    }

    auto channelToX = [&](double ch) -> int {
        const double t = (ch - x0) / (x1 - x0);
        return tv.dest.left() + int(std::lround(t * (tv.dest.width() - 1)));
    };
    const int left = channelToX(double(ch0));
    const int right = channelToX(double(ch1));
    const int top = tv.dest.top() + (row0 - tv.firstRow);
    const int bottom = tv.dest.top() + (row1 - tv.firstRow);
    return QRect(QPoint(qMin(left, right), top), QPoint(qMax(left, right), bottom)).normalized();
}

void SpectrumWaterfall::drawSelection(QPainter &p, const QRect &plot) const
{
    if (!m_selection.valid
        && !(m_selectDragging && m_selectDragMoved)
        && !m_selectMoving
        && !m_selectResizing) {
        return;
    }
    TimeView tv;
    if (!timeView(plot, &tv)) {
        return;
    }

    QRect r;
    if (m_selectDragging && m_selectDragMoved && !m_selectMoving && !m_selectResizing) {
        // Rubber-band in widget coords, clipped to dest (create mode only).
        r = QRect(m_selectPressPos, m_selectCurrPos).normalized();
        r = r.intersected(tv.dest);
    } else {
        r = selectionPixelRect(tv);
    }
    if (r.isEmpty()) {
        return;
    }

    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(r, QColor(255, 200, 60, 45));
    p.setPen(QPen(QColor(255, 210, 80, 230), 1, Qt::SolidLine));
    p.drawRect(r.adjusted(0, 0, -1, -1));

    // Caption with energy / channel and time span — updates live while dragging.
    if (!m_selection.valid) {
        return;
    }
    const double e0 = channelToEnergy(double(m_selection.ch0));
    const double e1 = channelToEnergy(double(m_selection.ch1));
    QString cap;
    if (hasEnergyAxis()) {
        cap = tr("%1–%2 keV · ch %3–%4 · %5 rows")
                  .arg(e0, 0, 'f', 1)
                  .arg(e1, 0, 'f', 1)
                  .arg(m_selection.ch0)
                  .arg(m_selection.ch1 - 1)
                  .arg(m_selection.row1 - m_selection.row0 + 1);
    } else {
        cap = tr("ch %1–%2 · %3 rows")
                  .arg(m_selection.ch0)
                  .arg(m_selection.ch1 - 1)
                  .arg(m_selection.row1 - m_selection.row0 + 1);
    }
    if (m_selection.row0 >= 0 && m_selection.row1 < m_rows.size()) {
        const QDateTime t0 = m_rows.at(m_selection.row0).wallTime;
        const QDateTime t1 = m_rows.at(m_selection.row1).wallTime;
        if (t0.isValid() && t1.isValid()) {
            cap += tr(" · %1–%2")
                       .arg(t0.toString(QStringLiteral("HH:mm:ss")),
                            t1.toString(QStringLiteral("HH:mm:ss")));
        }
    }
    const QFontMetrics fm(p.font());
    const int pad = 4;
    const int tw = fm.horizontalAdvance(cap) + 2 * pad;
    const int th = fm.height() + 2 * pad;
    int bx = r.left();
    int by = r.top() - th - 2;
    if (by < plot.top() + 2) {
        by = r.bottom() + 2;
    }
    if (bx + tw > plot.right()) {
        bx = plot.right() - tw;
    }
    if (bx < plot.left()) {
        bx = plot.left();
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 22, 28, 210));
    p.drawRoundedRect(QRect(bx, by, tw, th), 3, 3);
    p.setPen(QColor(255, 220, 140));
    p.drawText(bx + pad, by + pad + fm.ascent(), cap);
}

bool SpectrumWaterfall::saveHistory(QWidget *dialogParent)
{
    if (m_rows.isEmpty() || m_channels <= 0) {
        return false;
    }

    QString defaultName = QStringLiteral("spectrogram");
    if (!m_deviceSerial.isEmpty()) {
        defaultName += QLatin1Char('-') + m_deviceSerial;
    }
    if (m_rows.last().wallTime.isValid()) {
        defaultName += QLatin1Char('-')
            + m_rows.last().wallTime.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    }
    defaultName += QStringLiteral(".rcsg");

    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(
        dialogParent ? dialogParent : this,
        tr("Save spectrogram history"),
        docs.isEmpty() ? defaultName : (docs + QLatin1Char('/') + defaultName),
        SpectrogramFile::fileFilter());
    if (path.isEmpty()) {
        return false;
    }

    SpectrogramFile::Document doc;
    doc.nChannels = quint32(m_channels);
    doc.a0 = m_a0;
    doc.a1 = m_a1;
    doc.a2 = m_a2;
    doc.integrate = quint32(m_integrate);
    doc.historyMinutes = quint32(m_historyMinutes);
    doc.serial = m_deviceSerial;
    doc.rows.reserve(m_rows.size());
    for (const Row &r : m_rows) {
        SpectrogramFile::Row out;
        out.rates = r.rates;
        out.deltas = r.deltas;
        out.liveTimeSec = r.liveTimeSec;
        out.intervalSec = r.intervalSec;
        out.wallTime = r.wallTime;
        doc.rows.append(std::move(out));
    }

    QString err;
    if (!SpectrogramFile::save(path, doc, &err)) {
        QMessageBox::warning(dialogParent ? dialogParent : this, tr("Save history"),
                             tr("Failed to save:\n%1").arg(err));
        return false;
    }
    return true;
}

bool SpectrumWaterfall::loadHistory(QWidget *dialogParent)
{
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(
        dialogParent ? dialogParent : this,
        tr("Load spectrogram history"),
        docs,
        SpectrogramFile::fileFilter());
    if (path.isEmpty()) {
        return false;
    }

    QString loadPath = path;
    QString tempUnpacked;
    if (path.endsWith(QStringLiteral(".gz"), Qt::CaseInsensitive)) {
        tempUnpacked = QDir::temp().filePath(
            QStringLiteral("rcsg-load-%1.rcsg")
                .arg(QDateTime::currentMSecsSinceEpoch()));
        QString err;
        if (!SpectrogramCompress::gunzipFile(path, tempUnpacked, &err)) {
            QMessageBox::warning(dialogParent ? dialogParent : this, tr("Load history"),
                                 tr("Failed to decompress:\n%1").arg(err));
            return false;
        }
        loadPath = tempUnpacked;
    }

    SpectrogramFile::Document doc;
    QString err;
    const bool ok = SpectrogramFile::load(loadPath, &doc, &err);
    if (!tempUnpacked.isEmpty()) {
        QFile::remove(tempUnpacked);
    }
    if (!ok) {
        QMessageBox::warning(dialogParent ? dialogParent : this, tr("Load history"),
                             tr("Failed to load:\n%1").arg(err));
        return false;
    }
    if (doc.rows.isEmpty() || doc.nChannels == 0) {
        QMessageBox::warning(dialogParent ? dialogParent : this, tr("Load history"),
                             tr("File contains no spectrogram rows."));
        return false;
    }

    // Replace in-memory history (live acquisition can continue after a new baseline).
    m_rows.clear();
    m_channels = int(doc.nChannels);
    m_a0 = doc.a0;
    m_a1 = doc.a1;
    m_a2 = doc.a2;
    m_integrate = qBound(1, int(doc.integrate), kMaxIntegrate);
    m_historyMinutes = qBound(kMinHistoryMinutes, int(doc.historyMinutes), kMaxHistoryMinutes);
    recomputeCapacity();
    if (!doc.serial.isEmpty()) {
        m_deviceSerial = doc.serial;
    }

    m_rows.reserve(doc.rows.size());
    for (const SpectrogramFile::Row &r : doc.rows) {
        Row row;
        row.rates = r.rates;
        row.deltas = r.deltas;
        row.liveTimeSec = r.liveTimeSec;
        row.intervalSec = r.intervalSec;
        row.wallTime = r.wallTime;
        m_rows.append(std::move(row));
    }
    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
    }

    m_hasBaseline = false;
    m_baseline = Snapshot{};
    m_integrateProgress = 0;
    m_scrollFromNewest = 0;
    m_displayMax = matrixMaxRate();
    m_xMin = 0;
    m_xMax = m_channels;
    m_viewportFirstRow = -1;
    m_viewportCount = 0;
    m_image = QImage();
    clearCursor();

    rebuildViewportImage();
    emit followLiveChanged(true);
    emitScrollSignals();
    update();

    QString calLine;
    if (hasEnergyCalibration()) {
        calLine = tr("\nEnergy calibration: a0=%1  a1=%2  a2=%3")
                      .arg(double(m_a0), 0, 'g', 6)
                      .arg(double(m_a1), 0, 'g', 6)
                      .arg(double(m_a2), 0, 'g', 6);
    } else {
        calLine = tr("\nEnergy calibration: none in file (channel axis only).");
    }
    if (!m_deviceSerial.isEmpty()) {
        calLine += tr("\nSerial: %1").arg(m_deviceSerial);
    }

    QMessageBox::information(
        dialogParent ? dialogParent : this,
        tr("Load history"),
        tr("Loaded %1 rows (%2 channels) from\n%3%4")
            .arg(m_rows.size())
            .arg(m_channels)
            .arg(QFileInfo(path).fileName())
            .arg(calLine));
    return true;
}

QImage SpectrumWaterfall::renderRatesToImage(int firstRow, int nRows, int ch0, int ch1) const
{
    if (m_rows.isEmpty() || m_channels <= 0 || nRows <= 0) {
        return {};
    }
    firstRow = qBound(0, firstRow, m_rows.size() - 1);
    nRows = qMin(nRows, m_rows.size() - firstRow);
    if (nRows <= 0) {
        return {};
    }

    ch0 = qBound(0, ch0, m_channels);
    ch1 = qBound(ch0 + 1, ch1, m_channels);
    const int width = ch1 - ch0;
    if (width <= 0) {
        return {};
    }

    // Colour scale matches on-screen map (max over full history).
    QImage img(width, nRows, QImage::Format_RGB32);
    for (int y = 0; y < nRows; ++y) {
        const Row &row = m_rows.at(firstRow + y);
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        const int n = row.rates.size();
        for (int x = 0; x < width; ++x) {
            const int ch = ch0 + x;
            const float live = (ch < n) ? row.rates[ch] : 0.0f;
            line[x] = rateToColor(displayRate(live, ch));
        }
    }
    return img;
}

void SpectrumWaterfall::applyPngMetadata(QImage *img, int firstRow, int lastRow, int ch0,
                                         int ch1, PngExportScope scope) const
{
    if (!img) {
        return;
    }

    const QString app = QStringLiteral("Radiacode Monitor %1")
                            .arg(QApplication::applicationVersion());
    img->setText(QStringLiteral("Software"), app);
    img->setText(QStringLiteral("Title"),
                 scope == PngExportScope::FullHistory ? tr("Spectrogram full history")
                                                      : tr("Spectrogram view"));
    img->setText(QStringLiteral("ExportScope"),
                 scope == PngExportScope::FullHistory ? QStringLiteral("FullHistory")
                                                      : QStringLiteral("View"));
    img->setText(QStringLiteral("Source"), QStringLiteral("rate-data"));

    if (!m_deviceSerial.isEmpty()) {
        img->setText(QStringLiteral("Serial"), m_deviceSerial);
        img->setText(QStringLiteral("Device"), m_deviceSerial);
    }

    img->setText(QStringLiteral("View"),
                 netModeActive() ? QStringLiteral("Net") : QStringLiteral("Live"));
    img->setText(QStringLiteral("FollowingLive"),
                 isFollowingLive() ? QStringLiteral("yes") : QStringLiteral("no"));
    img->setText(QStringLiteral("IntegratePolls"), QString::number(m_integrate));
    img->setText(QStringLiteral("HistoryMinutes"), QString::number(m_historyMinutes));
    img->setText(QStringLiteral("Channels"), QString::number(m_channels));
    img->setText(QStringLiteral("ChannelStart"), QString::number(ch0));
    img->setText(QStringLiteral("ChannelEnd"), QString::number(ch1)); // exclusive
    img->setText(QStringLiteral("DisplayMaxCps"),
                 QString::number(double(m_displayMax), 'g', 6));
    img->setText(QStringLiteral("ImageWidth"), QString::number(img->width()));
    img->setText(QStringLiteral("ImageHeight"), QString::number(img->height()));
    img->setText(QStringLiteral("BufferRows"), QString::number(m_rows.size()));
    img->setText(QStringLiteral("RowStart"), QString::number(firstRow));
    img->setText(QStringLiteral("RowEnd"), QString::number(lastRow));

    if (hasEnergyAxis()) {
        img->setText(QStringLiteral("Calibration"),
                     QStringLiteral("a0=%1 a1=%2 a2=%3")
                         .arg(double(m_a0), 0, 'g', 8)
                         .arg(double(m_a1), 0, 'g', 8)
                         .arg(double(m_a2), 0, 'g', 8));
        img->setText(QStringLiteral("EnergyStartKeV"),
                     QString::number(channelToEnergy(double(ch0)), 'f', 3));
        img->setText(QStringLiteral("EnergyEndKeV"),
                     QString::number(channelToEnergy(double(ch1)), 'f', 3));
    }

    if (firstRow >= 0 && lastRow >= firstRow && lastRow < m_rows.size()) {
        const QDateTime t0 = m_rows.at(firstRow).wallTime;
        const QDateTime t1 = m_rows.at(lastRow).wallTime;
        if (t0.isValid() && t1.isValid()) {
            img->setText(QStringLiteral("TimeRange"),
                         QStringLiteral("%1 … %2")
                             .arg(t0.toString(Qt::ISODate), t1.toString(Qt::ISODate)));
            img->setText(QStringLiteral("TimeStart"), t0.toString(Qt::ISODate));
            img->setText(QStringLiteral("TimeEnd"), t1.toString(Qt::ISODate));
        }
        img->setText(QStringLiteral("ExportedRows"),
                     QString::number(lastRow - firstRow + 1));
    }

    img->setText(
        QStringLiteral("Description"),
        tr("Lossless PNG rasterized from spectrogram rate data (ΔN/Δt). "
           "Top row = older time, bottom = newer. "
           "Pixel colours use the current display scale (0 … max cps), not raw floats. "
           "Y = 1 pixel per history row; X = 1 pixel per channel."));
    img->setText(QStringLiteral("Orientation"),
                 QStringLiteral("top=oldest, bottom=newest, x=channel"));
}

bool SpectrumWaterfall::exportAsPng(PngExportScope scope, QWidget *dialogParent)
{
    if (m_rows.isEmpty() || m_channels <= 0) {
        return false;
    }

    int firstRow = 0;
    int nRows = m_rows.size();
    int ch0 = 0;
    int ch1 = m_channels;

    if (scope == PngExportScope::View) {
        TimeView tv;
        if (!timeView(plotRect(), &tv) || tv.visible <= 0) {
            QMessageBox::warning(dialogParent ? dialogParent : this, tr("Export PNG"),
                                 tr("No visible spectrogram rows to export."));
            return false;
        }
        firstRow = tv.firstRow;
        nRows = tv.visible;
        ch0 = qBound(0, int(std::floor(m_xMin)), m_channels);
        ch1 = qBound(ch0 + 1, int(std::ceil(m_xMax)), m_channels);
    }

    const int lastRow = firstRow + nRows - 1;

    // Large full-history images: confirm when height is big.
    if (scope == PngExportScope::FullHistory && nRows >= 2000) {
        const auto ans = QMessageBox::question(
            dialogParent ? dialogParent : this,
            tr("Export full history as PNG"),
            tr("The history buffer has %1 rows × %2 channels.\n"
               "The PNG will be about %3×%4 pixels and may take a moment.\n\n"
               "Continue?")
                .arg(nRows)
                .arg(ch1 - ch0)
                .arg(ch1 - ch0)
                .arg(nRows),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (ans != QMessageBox::Yes) {
            return false;
        }
    }

    QString defaultName = (scope == PngExportScope::FullHistory)
                              ? QStringLiteral("spectrogram-full")
                              : QStringLiteral("spectrogram-view");
    if (!m_deviceSerial.isEmpty()) {
        defaultName += QLatin1Char('-') + m_deviceSerial;
    }
    if (lastRow >= 0 && lastRow < m_rows.size() && m_rows.at(lastRow).wallTime.isValid()) {
        defaultName += QLatin1Char('-')
            + m_rows.at(lastRow).wallTime.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    }
    defaultName += QStringLiteral(".png");

    const QString startDir =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getSaveFileName(
        dialogParent ? dialogParent : this,
        scope == PngExportScope::FullHistory ? tr("Export full spectrogram history as PNG")
                                             : tr("Export spectrogram view as PNG"),
        startDir.isEmpty() ? defaultName : (startDir + QLatin1Char('/') + defaultName),
        tr("PNG images (*.png)"));
    if (path.isEmpty()) {
        return false;
    }

    QImage img = renderRatesToImage(firstRow, nRows, ch0, ch1);
    if (img.isNull()) {
        QMessageBox::warning(dialogParent ? dialogParent : this, tr("Export PNG"),
                             tr("Could not render spectrogram data."));
        return false;
    }

    applyPngMetadata(&img, firstRow, lastRow, ch0, ch1, scope);

    QImageWriter writer(path, "png");
    const auto keys = img.textKeys();
    for (const QString &key : keys) {
        writer.setText(key, img.text(key));
    }
    if (!writer.write(img)) {
        QMessageBox::warning(dialogParent ? dialogParent : this, tr("Export PNG"),
                             tr("Failed to write PNG:\n%1").arg(writer.errorString()));
        return false;
    }
    return true;
}

void SpectrumWaterfall::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    // Wheel over colour bar → scale colour range (SDR-style gain).
    if (colorBarRect().contains(event->position().toPoint())) {
        const float factor = (delta > 0) ? 0.85f : (1.0f / 0.85f);
        setColorCeilFraction(m_colorCeilFrac * factor);
        event->accept();
        return;
    }

    if (m_rows.isEmpty()) {
        event->ignore();
        return;
    }
    // Positive delta (wheel up) → look further into the past (increase scroll).
    const int steps = std::max(1, std::abs(delta) / 40);
    const int dir = (delta > 0) ? 1 : -1;
    setScrollFromNewest(m_scrollFromNewest + dir * steps);
    event->accept();
}

void SpectrumWaterfall::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    clampScroll();
    rebuildViewportImage();
    emitScrollSignals();
    update();
}

void SpectrumWaterfall::drawCursor(QPainter &p, const QRect &plot) const
{
    if (m_channels <= 0) {
        return;
    }

    const double x0 = std::clamp(m_xMin, 0.0, double(m_channels));
    const double x1 = std::clamp(m_xMax, 0.0, double(m_channels));
    if (x1 <= x0 + 1e-6) {
        return;
    }

    const QRect imgRect = plot.adjusted(1, 1, -1, -1);
    p.setRenderHint(QPainter::Antialiasing, true);

    auto channelToPlotX = [&](int ch) -> int {
        const double chMid = double(ch) + 0.5;
        const double tx = (chMid - x0) / (x1 - x0);
        return imgRect.left() + int(std::lround(tx * (imgRect.width() - 1)));
    };

    if (m_linkedCh >= 0 && m_linkedCh < m_channels && m_linkedCh != m_cursorCh) {
        const int xl = channelToPlotX(m_linkedCh);
        p.setPen(QPen(QColor(100, 220, 255, 200), 1, Qt::DotLine));
        p.drawLine(xl, plot.top(), xl, plot.bottom());
    }

    const bool hover = (m_cursorCh >= 0 && m_cursorRow >= 0 && m_cursorRow < m_rows.size());
    const bool linkOnly = (!hover && m_linkedCh >= 0 && m_linkedCh < m_channels);

    if (!hover && !linkOnly) {
        return;
    }

    const int ch = hover ? m_cursorCh : m_linkedCh;
    const int x = channelToPlotX(ch);

    p.setPen(QPen(linkOnly ? QColor(100, 220, 255, 220) : QColor(255, 200, 80, 220), 1,
                  Qt::DashLine));
    p.drawLine(x, plot.top(), x, plot.bottom());

    int y = plot.center().y();
    if (hover) {
        TimeView tv;
        if (timeView(plot, &tv) && m_cursorRow >= tv.firstRow
            && m_cursorRow < tv.firstRow + tv.visible) {
            y = tv.dest.top() + (m_cursorRow - tv.firstRow);
            p.drawLine(plot.left(), y, plot.right(), y);
        }
    }

    const int rowIdx = hover ? m_cursorRow : (m_rows.isEmpty() ? -1 : m_rows.size() - 1);
    if (rowIdx < 0) {
        return;
    }
    const Row &r = m_rows[rowIdx];
    const float live = (ch < r.rates.size()) ? r.rates[ch] : 0.0f;
    const float rate = displayRate(live, ch);
    const quint32 dN = (ch < r.deltas.size()) ? r.deltas[ch] : 0u;
    const int age = ageFromNewestSec(rowIdx);
    const double e = channelToEnergy(double(ch));

    const QString modeTag = netModeActive() ? tr("Net") : tr("Live");
    QString text;
    if (hasEnergyAxis()) {
        text = tr("%1 · E = %2 keV · ch %3 · %4 cps · ΔN = %5 · live %6 s · t−%7 s")
                   .arg(modeTag)
                   .arg(e, 0, 'f', 1)
                   .arg(ch)
                   .arg(rate, 0, 'f', 2)
                   .arg(dN)
                   .arg(r.liveTimeSec)
                   .arg(age);
    } else {
        text = tr("%1 · ch %2 · %3 cps · ΔN = %4 · live %5 s · t−%6 s")
                   .arg(modeTag)
                   .arg(ch)
                   .arg(rate, 0, 'f', 2)
                   .arg(dN)
                   .arg(r.liveTimeSec)
                   .arg(age);
    }
    if (r.wallTime.isValid()) {
        text += tr(" · %1").arg(r.wallTime.toString(QStringLiteral("HH:mm:ss")));
    }
    if (linkOnly) {
        text = tr("Linked (newest) · %1").arg(text);
    }

    const QFontMetrics fm(p.font());
    const int pad = 6;
    const int tw = fm.horizontalAdvance(text) + 2 * pad;
    const int th = fm.height() + 2 * pad;
    int bx = x + 8;
    int by = hover ? (y - th - 6) : (plot.top() + 8);
    if (bx + tw > plot.right() - 2) {
        bx = x - 8 - tw;
    }
    if (bx < plot.left() + 2) {
        bx = plot.left() + 2;
    }
    if (by < plot.top() + 2) {
        by = y + 8;
    }
    if (by + th > plot.bottom() - 2) {
        by = plot.bottom() - th - 2;
    }

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 22, 28, 220));
    p.drawRoundedRect(QRect(bx, by, tw, th), 4, 4);
    p.setPen(linkOnly ? QColor(160, 230, 255) : QColor(255, 220, 140));
    p.drawText(bx + pad, by + pad + fm.ascent(), text);
}

void SpectrumWaterfall::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 26, 30));

    const QRect plot = plotRect();
    p.setPen(QColor(55, 58, 64));
    p.setBrush(QColor(20, 22, 26));
    p.drawRect(plot);

    if (m_rows.isEmpty() || m_channels <= 0) {
        p.setPen(QColor(140, 142, 150));
        p.drawText(plot, Qt::AlignCenter,
                   tr("Spectrogram — waiting for spectrum updates…\n"
                      "Wheel scrolls history once data is available."));
        return;
    }

    const double x0 = std::clamp(m_xMin, 0.0, double(m_channels));
    const double x1 = std::clamp(m_xMax, 0.0, double(m_channels));
    if (x1 <= x0 + 1e-6) {
        return;
    }

    TimeView tv;
    if (!timeView(plot, &tv)) {
        return;
    }

    // Rebuild cache if viewport window or size drifted (e.g. first paint after data).
    if (m_image.isNull() || m_viewportFirstRow != tv.firstRow
        || m_viewportCount != tv.visible || m_image.height() != tv.visible) {
        rebuildViewportImage();
    }

    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (!m_image.isNull()) {
        const QRectF src(x0, 0.0, x1 - x0, double(m_image.height()));
        p.drawImage(tv.dest, m_image, src);
    }

    drawColorBar(p, plot);

    const QFontMetrics fm = p.fontMetrics();

    // Mode / follow badge (top-left of plot).
    {
        QString badge = netModeActive() ? tr("Net") : tr("Live");
        if (!isFollowingLive()) {
            badge = tr("%1 · hist").arg(badge);
        }
        const int pad = 4;
        const int tw = fm.horizontalAdvance(badge) + 2 * pad;
        const int th = fm.height() + 2 * pad;
        const QRect br(plot.left() + 4, plot.top() + 4, tw, th);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(br, 3, 3);
        p.setPen(isFollowingLive()
                     ? (netModeActive() ? QColor(160, 230, 255) : QColor(200, 200, 200))
                     : QColor(255, 200, 100));
        p.drawText(br.left() + pad, br.top() + pad + fm.ascent(), badge);
    }

    // Y axis: wall-clock HH:mm for oldest / mid / newest *visible* rows.
    {
        QVector<int> tickRows;
        tickRows.append(tv.firstRow);
        if (tv.visible >= 3) {
            tickRows.append(tv.firstRow + tv.visible / 2);
        }
        if (tv.visible >= 2) {
            tickRows.append(tv.firstRow + tv.visible - 1);
        }

        QString lastLabel;
        for (int rowIdx : tickRows) {
            if (rowIdx < 0 || rowIdx >= m_rows.size()) {
                continue;
            }
            const QDateTime wt = m_rows.at(rowIdx).wallTime;
            if (!wt.isValid()) {
                continue;
            }
            const QString label = wt.toString(QStringLiteral("HH:mm"));
            if (label == lastLabel) {
                continue;
            }
            lastLabel = label;

            int y = tv.dest.top() + (rowIdx - tv.firstRow);
            y = std::clamp(y, tv.imgRect.top() + fm.ascent() / 2,
                           tv.imgRect.bottom() - fm.descent());

            p.setPen(QColor(55, 58, 64));
            p.drawLine(plot.left(), y, plot.left() + 4, y);
            p.setPen(QColor(170, 172, 180));
            const int tw = fm.horizontalAdvance(label);
            p.drawText(plot.left() - 6 - tw, y + fm.ascent() / 2 - 1, label);
        }
    }

    // X labels (energy / channel).
    p.setPen(QColor(170, 172, 180));
    const int ticks = 5;
    for (int t = 0; t <= ticks; ++t) {
        const double ch = x0 + (x1 - x0) * (double(t) / ticks);
        const int x = plot.left()
            + int((ch - x0) / (x1 - x0) * (plot.width() - 1));
        QString label;
        if (hasEnergyAxis()) {
            label = QString::number(channelToEnergy(ch), 'f', ch >= 100 ? 0 : 1);
        } else {
            label = QString::number(int(std::lround(ch)));
        }
        const int tw = fm.horizontalAdvance(label);
        p.drawText(x - tw / 2, plot.bottom() + fm.ascent() + 2, label);
    }

    // Selection underlay first; cursor bubble on top after selection is finalized
    // (during create/move/resize drag the cursor overlay stays hidden so caption is readable).
    drawSelection(p, plot);
    if (!m_selectDragging && !m_selectMoving && !m_selectResizing) {
        drawCursor(p, plot);
    }
}
