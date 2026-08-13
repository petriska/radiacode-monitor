#include "spectrumwaterfall.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>

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
        "Mouse wheel: scroll history · Double-click: jump to live.\n"
        "Time is 1:1 (one row = one pixel). Colour scale = 0 … max over history.\n"
        "X range follows spectrum zoom. Hover for energy, rate, ΔN, time."));
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
    clearCursor();
    if (!wasFollow) {
        emit followLiveChanged(true);
    }
    emitScrollSignals();
    update();
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

QRgb SpectrumWaterfall::rateToColor(float rate) const
{
    const float t = m_displayMax > 1e-12f
        ? std::clamp(rate / m_displayMax, 0.0f, 1.0f)
        : 0.0f;
    const float u = std::pow(t, 0.55f);

    int r = 0;
    int g = 0;
    int b = 0;
    if (u < 0.25f) {
        const float s = u / 0.25f;
        b = int(80 + 175 * s);
    } else if (u < 0.5f) {
        const float s = (u - 0.25f) / 0.25f;
        g = int(255 * s);
        b = 255;
    } else if (u < 0.75f) {
        const float s = (u - 0.5f) / 0.25f;
        r = int(255 * s);
        g = 255;
        b = int(255 * (1.0f - s));
    } else {
        const float s = (u - 0.75f) / 0.25f;
        r = 255;
        g = 255;
        b = int(255 * s);
    }
    return qRgb(r, g, b);
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

    m_rows.append(std::move(row));
    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
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

void SpectrumWaterfall::mouseMoveEvent(QMouseEvent *event)
{
    setCursorFromPos(event->pos());
    event->accept();
}

void SpectrumWaterfall::leaveEvent(QEvent *event)
{
    clearCursor();
    QWidget::leaveEvent(event);
}

void SpectrumWaterfall::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        followLive();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SpectrumWaterfall::wheelEvent(QWheelEvent *event)
{
    // Vertical wheel pans time history (SDR-style). Angle delta: 120 ≈ one notch.
    const int delta = event->angleDelta().y();
    if (delta == 0 || m_rows.isEmpty()) {
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

    drawCursor(p, plot);
}
