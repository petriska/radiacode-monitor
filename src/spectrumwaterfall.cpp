#include "spectrumwaterfall.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>

SpectrumWaterfall::SpectrumWaterfall(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setToolTip(tr(
        "Waterfall: count rate per channel over time (ΔN / Δt).\n"
        "Follows spectrum view: Live or Net (when a background is loaded).\n"
        "Newest row at the bottom. Time is 1:1 (one row = one pixel) so history\n"
        "only scrolls — it is not squeezed to fill the pane.\n"
        "Colour scale = 0 … max rate over history; the image is recoloured when\n"
        "that max changes (e.g. a source is brought near), with ~2% hysteresis.\n"
        "Hover for energy, rate, ΔN, and time. X range follows spectrum zoom."));
}

void SpectrumWaterfall::setMaxRows(int rows)
{
    m_maxRows = qBound(32, rows, 2000);
    while (m_snaps.size() > maxSnaps()) {
        m_snaps.removeFirst();
    }
    rebuildRowsFromSnapshots();
}

void SpectrumWaterfall::setIntegrateCount(int n)
{
    const int k = qBound(1, n, kMaxIntegrate);
    if (m_integrate == k) {
        return;
    }
    m_integrate = k;
    while (m_snaps.size() > maxSnaps()) {
        m_snaps.removeFirst();
    }
    rebuildRowsFromSnapshots();
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
    m_snaps.clear();
    m_rows.clear();
    m_channels = 0;
    m_displayMax = 1.0f;
    m_image = QImage();
    m_hasBaseline = false;
    m_baseline = Snapshot{};
    m_integrateProgress = 0;
    m_linkedCh = -1;
    clearCursor();
    update();
}

void SpectrumWaterfall::setBackground(const QVector<quint32> &counts, quint32 durationSec)
{
    m_bgCounts = counts;
    m_bgDurationSec = durationSec;
    rebuildRowsFromSnapshots();
}

void SpectrumWaterfall::clearBackground()
{
    m_bgCounts.clear();
    m_bgDurationSec = 0;
    rebuildRowsFromSnapshots();
}

void SpectrumWaterfall::setDisplayMode(DisplayMode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    rebuildRowsFromSnapshots();
}

bool SpectrumWaterfall::netModeActive() const
{
    return m_mode == DisplayMode::Net && !m_bgCounts.isEmpty() && m_bgDurationSec > 0;
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

QVector<quint32> SpectrumWaterfall::netCountsFor(const Snapshot &snap) const
{
    const int n = snap.counts.size();
    QVector<quint32> net(n);
    if (m_bgCounts.isEmpty() || m_bgDurationSec == 0) {
        return snap.counts;
    }
    const double scale = double(snap.durationSec) / double(m_bgDurationSec);
    const int nBg = m_bgCounts.size();
    for (int i = 0; i < n; ++i) {
        const double bg = (i < nBg) ? double(m_bgCounts.at(i)) * scale : 0.0;
        const double v = double(snap.counts.at(i)) - bg;
        net[i] = v > 0.0 ? static_cast<quint32>(v + 0.5) : 0u;
    }
    return net;
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

    const bool useNet = netModeActive();
    const QVector<quint32> a = useNet ? netCountsFor(prev) : prev.counts;
    const QVector<quint32> b = useNet ? netCountsFor(cur) : cur.counts;
    const int n = qMin(m_channels, qMin(a.size(), b.size()));

    out->rates.resize(m_channels);
    out->deltas.resize(m_channels);
    out->liveTimeSec = cur.durationSec;
    out->intervalSec = static_cast<quint32>(dT);
    out->wallTime = cur.wallTime;

    for (int ch = 0; ch < m_channels; ++ch) {
        quint32 dN = 0;
        if (ch < n) {
            const qint64 d = qint64(b.at(ch)) - qint64(a.at(ch));
            dN = d > 0 ? static_cast<quint32>(d) : 0u;
        }
        out->deltas[ch] = dN;
        out->rates[ch] = float(dN) / float(dT);
    }
    return true;
}

void SpectrumWaterfall::ensureImage()
{
    if (m_channels <= 0 || m_maxRows <= 0) {
        m_image = QImage();
        return;
    }
    if (m_image.width() == m_channels && m_image.height() == m_maxRows
        && m_image.format() == QImage::Format_RGB32) {
        return;
    }
    m_image = QImage(m_channels, m_maxRows, QImage::Format_RGB32);
    m_image.fill(QColor(20, 22, 26));
}

float SpectrumWaterfall::matrixMaxRate() const
{
    float mx = 0.0f;
    for (const Row &row : m_rows) {
        for (float rate : row.rates) {
            mx = std::max(mx, rate);
        }
    }
    if (mx < 1e-6f) {
        mx = 1e-6f;
    }
    return mx;
}

void SpectrumWaterfall::rebuildImage()
{
    if (m_channels <= 0 || m_maxRows <= 0) {
        m_image = QImage();
        return;
    }
    m_image = QImage(m_channels, m_maxRows, QImage::Format_RGB32);
    m_image.fill(QColor(20, 22, 26));

    const int nRows = m_rows.size();
    const int y0 = m_maxRows - nRows;
    for (int r = 0; r < nRows; ++r) {
        const Row &row = m_rows[r];
        const int y = y0 + r;
        if (y < 0 || y >= m_maxRows) {
            continue;
        }
        auto *line = reinterpret_cast<QRgb *>(m_image.scanLine(y));
        const int n = qMin(m_channels, row.rates.size());
        for (int x = 0; x < m_channels; ++x) {
            const float rate = (x < n) ? row.rates[x] : 0.0f;
            line[x] = rateToColor(rate);
        }
    }
}

bool SpectrumWaterfall::timeView(const QRect &plot, TimeView *tv) const
{
    if (!tv || m_rows.isEmpty() || m_image.isNull() || m_channels <= 0) {
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

    // Data always lives in the bottom nRows scanlines of m_image.
    // Show the newest `visible` lines, 1 source row → 1 dest pixel (no Y stretch).
    const int srcY = m_image.height() - visible;
    const int destTop = imgRect.top() + plotH - visible;

    tv->imgRect = imgRect;
    tv->dest = QRect(imgRect.left(), destTop, imgRect.width(), visible);
    tv->visible = visible;
    tv->firstRow = nRows - visible;
    tv->srcY = srcY;
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
    const float rate = (m_cursorCh < r.rates.size()) ? r.rates[m_cursorCh] : 0.0f;
    const quint32 dN = (m_cursorCh < r.deltas.size()) ? r.deltas[m_cursorCh] : 0u;
    const double e = channelToEnergy(double(m_cursorCh));
    emit cursorInfoChanged(m_cursorCh, e, rate, dN, r.liveTimeSec,
                           ageFromNewestSec(m_cursorRow));
}

void SpectrumWaterfall::appendDisplayRow(Row &&row)
{
    const bool droppedOldest = (m_rows.size() >= m_maxRows);

    m_rows.append(std::move(row));
    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
    }

    // Colour map = [0, max over entire history]. Recolour all rows when that
    // max moves enough (source near detector, peak leaves the buffer, …).
    // Geometry stays 1:1 — only colours change, not vertical squeeze.
    const float newMax = matrixMaxRate();
    const float oldMax = m_displayMax;
    const bool scaleChanged =
        (oldMax < 1e-6f)
        || (newMax > oldMax * (1.0f + kScaleHysteresis))
        || (newMax < oldMax * (1.0f - kScaleHysteresis));

    if (scaleChanged) {
        m_displayMax = newMax;
        rebuildImage();
        refreshCursorAfterScroll(droppedOldest);
        update();
        return;
    }

    // Scale stable: scroll image + paint only the new bottom line.
    ensureImage();
    if (m_image.isNull()) {
        refreshCursorAfterScroll(droppedOldest);
        update();
        return;
    }

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
        const float rate = (x < n) ? newest.rates[x] : 0.0f;
        line[x] = rateToColor(rate);
    }

    refreshCursorAfterScroll(droppedOldest);
    update();
}

void SpectrumWaterfall::resetBaselineFromLatest()
{
    if (m_snaps.isEmpty()) {
        m_hasBaseline = false;
        m_baseline = Snapshot{};
        m_integrateProgress = 0;
        return;
    }
    m_baseline = m_snaps.last();
    m_hasBaseline = true;
    m_integrateProgress = 0;
}

void SpectrumWaterfall::rebuildRowsFromSnapshots()
{
    m_rows.clear();
    m_displayMax = 1.0f;

    const int K = qMax(1, m_integrate);
    if (m_snaps.size() < K + 1 || m_channels <= 0) {
        m_image = QImage();
        ensureImage();
        resetBaselineFromLatest();
        update();
        return;
    }

    // Non-overlapping bins: snap[i-K] → snap[i], step K.
    for (int i = K; i < m_snaps.size(); i += K) {
        Row row;
        if (!makeRow(m_snaps.at(i - K), m_snaps.at(i), &row)) {
            continue; // reset or clock went backwards — skip this bin
        }
        m_rows.append(std::move(row));
    }

    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
    }
    m_displayMax = matrixMaxRate();

    // Resume incremental updates from the last snap used as a bin end, else latest.
    if (!m_snaps.isEmpty()) {
        const int lastEnd = (m_snaps.size() - 1) / K * K;
        if (lastEnd >= 0 && lastEnd < m_snaps.size()) {
            m_baseline = m_snaps.at(lastEnd);
            m_hasBaseline = true;
            m_integrateProgress = (m_snaps.size() - 1) - lastEnd;
        } else {
            resetBaselineFromLatest();
        }
    } else {
        resetBaselineFromLatest();
    }

    rebuildImage();
    update();
}

void SpectrumWaterfall::pushSpectrum(const QVector<quint32> &counts, quint32 durationSec)
{
    if (counts.isEmpty()) {
        return;
    }

    const int n = counts.size();
    if (n != m_channels) {
        m_snaps.clear();
        m_rows.clear();
        m_channels = n;
        m_hasBaseline = false;
        m_baseline = Snapshot{};
        m_integrateProgress = 0;
        m_displayMax = 1.0f;
        m_image = QImage();
        clearCursor();
        if (m_xMax <= m_xMin || m_xMax > n) {
            m_xMin = 0;
            m_xMax = n;
        }
    }

    Snapshot snap;
    snap.counts = counts;
    snap.durationSec = durationSec;
    snap.wallTime = QDateTime::currentDateTime();
    m_snaps.append(std::move(snap));

    while (m_snaps.size() > maxSnaps()) {
        m_snaps.removeFirst();
    }

    const Snapshot &cur = m_snaps.last();

    if (!m_hasBaseline) {
        m_baseline = cur;
        m_hasBaseline = true;
        m_integrateProgress = 0;
        ensureImage();
        update();
        return;
    }

    ++m_integrateProgress;
    if (m_integrateProgress < m_integrate) {
        return;
    }
    m_integrateProgress = 0;

    Row row;
    if (!makeRow(m_baseline, cur, &row)) {
        // Spectrum reset or live-time did not advance — resync baseline.
        m_baseline = cur;
        return;
    }

    m_baseline = cur;
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
    const int local = y - tv.dest.top(); // 0 = oldest visible
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
    const float rate = (ch < r.rates.size()) ? r.rates[ch] : 0.0f;
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

void SpectrumWaterfall::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
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
    const float rate = (ch < r.rates.size()) ? r.rates[ch] : 0.0f;
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

    if (m_image.isNull() || m_channels <= 0) {
        p.setPen(QColor(140, 142, 150));
        p.drawText(plot, Qt::AlignCenter,
                   tr("Waterfall — waiting for spectrum updates…"));
        return;
    }

    const double x0 = std::clamp(m_xMin, 0.0, double(m_channels));
    const double x1 = std::clamp(m_xMax, 0.0, double(m_channels));
    if (x1 <= x0 + 1e-6) {
        return;
    }

    // Y: 1:1 bottom-aligned (no vertical squeeze). X: stretch to plot (energy zoom).
    TimeView tv;
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (timeView(plot, &tv)) {
        const QRectF src(x0, double(tv.srcY), x1 - x0, double(tv.visible));
        p.drawImage(tv.dest, m_image, src);
    }

    const QFontMetrics fm = p.fontMetrics();

    // Mode badge (top-left of plot).
    {
        const QString badge = netModeActive() ? tr("Net") : tr("Live");
        const int pad = 4;
        const int tw = fm.horizontalAdvance(badge) + 2 * pad;
        const int th = fm.height() + 2 * pad;
        const QRect br(plot.left() + 4, plot.top() + 4, tw, th);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(br, 3, 3);
        p.setPen(netModeActive() ? QColor(160, 230, 255) : QColor(200, 200, 200));
        p.drawText(br.left() + pad, br.top() + pad + fm.ascent(), badge);
    }

    // Y axis: wall-clock HH:mm for oldest / mid / newest *visible* rows.
    if (timeView(plot, &tv) && tv.visible > 0) {
        QVector<int> tickRows;
        tickRows.append(tv.firstRow); // oldest visible
        if (tv.visible >= 3) {
            tickRows.append(tv.firstRow + tv.visible / 2);
        }
        if (tv.visible >= 2) {
            tickRows.append(tv.firstRow + tv.visible - 1); // newest
        }

        p.setPen(QColor(170, 172, 180));
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
