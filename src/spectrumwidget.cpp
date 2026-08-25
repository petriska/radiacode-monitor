#include "spectrumwidget.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

// std::min used in barColorForChannel

namespace {
// Extra room: left for rotated "Counts" title; bottom for tick numbers + "Energy (keV)".
constexpr int kMarginLeft = 64;
constexpr int kMarginRight = 14;
constexpr int kMarginTop = 14;
constexpr int kMarginBottom = 48;
constexpr double kMinSpanChannels = 8.0;
constexpr double kZoomFactor = 1.18;
} // namespace

SpectrumWidget::SpectrumWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Wheel: zoom X · Drag: pan · Double-click: reset view · Cursor: energy (keV)"));
}

void SpectrumWidget::setSpectrum(const QVector<quint32> &counts, float a0, float a1, float a2)
{
    const int oldN = channelCount();
    m_counts = counts;
    m_a0 = a0;
    m_a1 = a1;
    m_a2 = a2;

    const int n = channelCount();
    if (n <= 0) {
        m_xMin = 0;
        m_xMax = 1;
        clearCursor();
        update();
        return;
    }

    // Keep zoom when channel count is unchanged (live spectrum refresh).
    if (oldN != n || m_xMax <= m_xMin) {
        m_xMin = 0;
        m_xMax = static_cast<double>(n);
        emitViewRange();
    } else {
        clampView();
    }
    update();
}

void SpectrumWidget::setRoiBands(const QVector<SpectrumRoiBand> &bands)
{
    m_roiBands = bands;
    update();
}

void SpectrumWidget::setBaseBarColor(const QColor &color)
{
    m_baseBarColor = color.isValid() ? color : QColor(70, 150, 255, 210);
    // Slight lift for cursor/highlight on the same hue.
    m_baseBarColorHi = QColor(std::min(255, m_baseBarColor.red() + 40),
                              std::min(255, m_baseBarColor.green() + 40),
                              std::min(255, m_baseBarColor.blue() + 40),
                              std::min(255, m_baseBarColor.alpha() + 20));
    update();
}

void SpectrumWidget::setLogYScale(bool on)
{
    if (m_logY == on) {
        return;
    }
    m_logY = on;
    update();
}

double SpectrumWidget::yNorm(double counts, double maxC) const
{
    if (!(maxC > 0.0) || !(counts >= 0.0) || !std::isfinite(counts)) {
        return 0.0;
    }
    if (m_logY) {
        // log1p = ln(1+x): zeros map to 0, no clamp tricks.
        const double den = std::log1p(maxC);
        if (!(den > 0.0)) {
            return 0.0;
        }
        return std::clamp(std::log1p(counts) / den, 0.0, 1.0);
    }
    return std::clamp(std::sqrt(counts / maxC), 0.0, 1.0);
}

double SpectrumWidget::yDenorm(double u, double maxC) const
{
    const double t = std::clamp(u, 0.0, 1.0);
    if (!(maxC > 0.0)) {
        return 0.0;
    }
    if (m_logY) {
        return std::expm1(t * std::log1p(maxC));
    }
    return t * t * maxC;
}

void SpectrumWidget::clear()
{
    m_counts.clear();
    m_roiBands.clear();
    m_xMin = 0;
    m_xMax = 1;
    m_linkedCh = -1;
    clearCursor();
    emitViewRange();
    update();
}

void SpectrumWidget::setLinkedChannel(int channel)
{
    int ch = channel;
    if (ch >= 0 && ch >= channelCount()) {
        ch = -1;
    }
    if (m_linkedCh == ch) {
        return;
    }
    m_linkedCh = ch;
    update();
}

void SpectrumWidget::resetView()
{
    if (channelCount() > 0) {
        m_xMin = 0;
        m_xMax = static_cast<double>(channelCount());
    } else {
        m_xMin = 0;
        m_xMax = 1;
    }
    emitViewRange();
    update();
}

void SpectrumWidget::setViewRange(double xMin, double xMax)
{
    if (channelCount() <= 0) {
        return;
    }
    m_xMin = xMin;
    m_xMax = xMax;
    clampView();
    emitViewRange();
    update();
}

int SpectrumWidget::channelCount() const
{
    return m_counts.size();
}

bool SpectrumWidget::hasEnergyAxis() const
{
    // Device calibration: non-trivial linear/quadratic term, or non-zero offset with slope.
    return std::fabs(static_cast<double>(m_a1)) > 1e-12
        || std::fabs(static_cast<double>(m_a2)) > 1e-12;
}

double SpectrumWidget::channelToEnergy(double channel) const
{
    // Fractional channel via continuous polynomial (same form as spectrumChannelToEnergy).
    const double ch = channel;
    return static_cast<double>(m_a0) + static_cast<double>(m_a1) * ch
        + static_cast<double>(m_a2) * ch * ch;
}

QRect SpectrumWidget::plotRect() const
{
    return QRect(kMarginLeft,
                 kMarginTop,
                 std::max(1, width() - kMarginLeft - kMarginRight),
                 std::max(1, height() - kMarginTop - kMarginBottom));
}

void SpectrumWidget::clampView()
{
    const int n = channelCount();
    if (n <= 0) {
        m_xMin = 0;
        m_xMax = 1;
        return;
    }
    const double nD = static_cast<double>(n);
    if (m_xMax < m_xMin) {
        std::swap(m_xMin, m_xMax);
    }
    double span = m_xMax - m_xMin;
    if (span < kMinSpanChannels) {
        const double mid = 0.5 * (m_xMin + m_xMax);
        m_xMin = mid - 0.5 * kMinSpanChannels;
        m_xMax = mid + 0.5 * kMinSpanChannels;
        span = kMinSpanChannels;
    }
    if (span > nD) {
        m_xMin = 0;
        m_xMax = nD;
        return;
    }
    if (m_xMin < 0) {
        m_xMax -= m_xMin;
        m_xMin = 0;
    }
    if (m_xMax > nD) {
        m_xMin -= (m_xMax - nD);
        m_xMax = nD;
        if (m_xMin < 0) {
            m_xMin = 0;
        }
    }
}

void SpectrumWidget::emitViewRange()
{
    emit viewRangeChanged(m_xMin, m_xMax);
}

double SpectrumWidget::channelToX(double channel, const QRect &plot) const
{
    const double span = std::max(1e-9, m_xMax - m_xMin);
    const double t = (channel - m_xMin) / span;
    return plot.left() + t * plot.width();
}

double SpectrumWidget::xToChannel(int x, const QRect &plot) const
{
    if (plot.width() <= 0) {
        return m_xMin;
    }
    const double t = (static_cast<double>(x) - plot.left()) / static_cast<double>(plot.width());
    return m_xMin + t * (m_xMax - m_xMin);
}

quint32 SpectrumWidget::maxCountInView() const
{
    const int n = channelCount();
    if (n <= 0) {
        return 1;
    }
    const int i0 = std::max(0, static_cast<int>(std::floor(m_xMin)));
    const int i1 = std::min(n, static_cast<int>(std::ceil(m_xMax)));
    quint32 maxC = 1;
    for (int i = i0; i < i1; ++i) {
        maxC = std::max(maxC, m_counts[i]);
    }
    return maxC;
}

void SpectrumWidget::clearCursor()
{
    if (m_cursorCh == -1) {
        return;
    }
    m_cursorCh = -1;
    emit cursorInfoChanged(-1, 0.0, 0);
    update();
}

void SpectrumWidget::setCursorFromPos(const QPoint &pos)
{
    const QRect plot = plotRect();
    if (m_counts.isEmpty() || !plot.contains(pos)) {
        clearCursor();
        return;
    }
    const double chF = xToChannel(pos.x(), plot);
    int ch = static_cast<int>(std::floor(chF));
    ch = std::clamp(ch, 0, channelCount() - 1);
    if (ch == m_cursorCh) {
        return;
    }
    m_cursorCh = ch;
    const double e = channelToEnergy(static_cast<double>(ch));
    const quint32 c = m_counts[ch];
    emit cursorInfoChanged(ch, e, c);
    update();
}

void SpectrumWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    drawBackground(p);

    if (m_counts.isEmpty()) {
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter, tr("No spectrum yet"));
        return;
    }

    const QRect plot = plotRect();
    const quint32 maxC = maxCountInView();
    drawGridAndAxes(p, plot, maxC);
    drawSpectrum(p, plot, maxC);
    drawCursor(p, plot);
}

void SpectrumWidget::drawBackground(QPainter &p) const
{
    p.fillRect(rect(), QColor(24, 26, 30));
}

void SpectrumWidget::drawGridAndAxes(QPainter &p, const QRect &plot, quint32 maxC) const
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QColor(55, 58, 64));
    p.setBrush(QColor(30, 32, 38));
    p.drawRect(plot);

    // Vertical grid + X labels
    const int xTicks = 6;
    p.setFont(font());
    for (int t = 0; t <= xTicks; ++t) {
        const double ch = m_xMin + (m_xMax - m_xMin) * (static_cast<double>(t) / xTicks);
        const int x = static_cast<int>(std::lround(channelToX(ch, plot)));
        p.setPen(QColor(45, 48, 54));
        p.drawLine(x, plot.top(), x, plot.bottom());
        p.setPen(QColor(170, 172, 180));
        QString label;
        if (hasEnergyAxis()) {
            label = QString::number(channelToEnergy(ch), 'f', ch >= 100 ? 0 : 1);
        } else {
            label = QString::number(static_cast<int>(std::lround(ch)));
        }
        const QFontMetrics fm = p.fontMetrics();
        const int tw = fm.horizontalAdvance(label);
        // Tick numbers just under the plot (above the axis title).
        p.drawText(x - tw / 2, plot.bottom() + fm.ascent() + 4, label);
    }

    // Horizontal grid + Y labels (√ or log1p space → show actual count levels)
    const int yTicks = 4;
    const QFontMetrics fmY = p.fontMetrics();
    for (int t = 0; t <= yTicks; ++t) {
        const double u = static_cast<double>(t) / yTicks; // 0..1 in transform space
        const int y = plot.bottom() - static_cast<int>(u * (plot.height() - 1));
        p.setPen(QColor(45, 48, 54));
        p.drawLine(plot.left(), y, plot.right(), y);
        const double countVal = yDenorm(u, static_cast<double>(maxC));
        p.setPen(QColor(170, 172, 180));
        const QString label = (countVal >= 1000.0)
            ? QString::number(countVal / 1000.0, 'f', 1) + QStringLiteral("k")
            : QString::number(static_cast<int>(std::lround(countVal)));
        // Right-align count numbers next to the plot (leave strip for Y title on far left).
        const int tw = fmY.horizontalAdvance(label);
        p.drawText(plot.left() - 6 - tw, y + fmY.ascent() / 2, label);
    }

    p.setPen(QColor(130, 132, 140));
    const QString xTitle = hasEnergyAxis() ? tr("Energy (keV)") : tr("Channel");
    const QFontMetrics fmTitle = p.fontMetrics();
    const int xTitleW = fmTitle.horizontalAdvance(xTitle);
    // Axis title below tick numbers so they do not overlap.
    p.drawText(plot.center().x() - xTitleW / 2, height() - 6, xTitle);

    // Rotated Y title on the far left edge.
    const QString yTitle = m_logY ? tr("Counts (log₁ₚ y+1)") : tr("Counts (√ scale)");
    p.save();
    p.translate(12, plot.center().y() + fmTitle.horizontalAdvance(yTitle) / 2);
    p.rotate(-90);
    p.drawText(0, 0, yTitle);
    p.restore();
}

QColor SpectrumWidget::barColorForChannel(int channel, bool highlight) const
{
    const QColor base = m_baseBarColor;
    const QColor baseHi = m_baseBarColorHi;

    if (m_roiBands.isEmpty()) {
        return highlight ? baseHi : base;
    }

    // Channel energy at center of bin for energy-based ROI membership.
    const double e = channelToEnergy(static_cast<double>(channel) + 0.5);
    const bool energyOk = hasEnergyAxis();
    int rSum = 0;
    int gSum = 0;
    int bSum = 0;
    int nHit = 0;
    for (const SpectrumRoiBand &band : m_roiBands) {
        if (!band.enabled) {
            continue;
        }
        bool inBand = false;
        if (band.chMin >= 0) {
            // Channel window [chMin, chMax) — used by selection spectrum highlight.
            if (band.chMax > band.chMin && channel >= band.chMin && channel < band.chMax) {
                inBand = true;
            }
        } else if (energyOk && band.eMaxKeV > band.eMinKeV) {
            if (e >= band.eMinKeV && e < band.eMaxKeV) {
                inBand = true;
            }
        }
        if (inBand) {
            rSum += band.color.red();
            gSum += band.color.green();
            bSum += band.color.blue();
            ++nHit;
        }
    }
    if (nHit == 0) {
        return highlight ? baseHi : base;
    }

    QColor c(rSum / nHit, gSum / nHit, bSum / nHit, highlight ? 240 : 210);
    if (highlight) {
        // Slight lift toward white for cursor channel.
        c = QColor(std::min(255, c.red() + 40),
                   std::min(255, c.green() + 40),
                   std::min(255, c.blue() + 40), 240);
    }
    return c;
}

void SpectrumWidget::drawSpectrum(QPainter &p, const QRect &plot, quint32 maxC) const
{
    const int n = channelCount();
    const int i0 = std::max(0, static_cast<int>(std::floor(m_xMin)));
    const int i1 = std::min(n, static_cast<int>(std::ceil(m_xMax)));
    if (i0 >= i1) {
        return;
    }

    p.setPen(Qt::NoPen);

    for (int i = i0; i < i1; ++i) {
        const double x0 = channelToX(static_cast<double>(i), plot);
        const double x1 = channelToX(static_cast<double>(i + 1), plot);
        const int left = static_cast<int>(std::floor(x0));
        const int right = static_cast<int>(std::ceil(x1));
        const int w = std::max(1, right - left);
        const double t = yNorm(static_cast<double>(m_counts[i]), static_cast<double>(maxC));
        const int h = static_cast<int>(t * (plot.height() - 1));
        if (h <= 0) {
            continue;
        }
        const bool hi = (i == m_cursorCh) || (i == m_linkedCh);
        p.fillRect(left, plot.bottom() - h, w, h, barColorForChannel(i, hi));
    }

    p.setPen(QColor(70, 72, 80));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
}

void SpectrumWidget::drawCursor(QPainter &p, const QRect &plot) const
{
    const int n = channelCount();
    if (n <= 0) {
        return;
    }

    p.setRenderHint(QPainter::Antialiasing, true);

    // Linked line from waterfall (cyan) — always full height when set.
    if (m_linkedCh >= 0 && m_linkedCh < n && m_linkedCh != m_cursorCh) {
        const double xMidL = 0.5
            * (channelToX(static_cast<double>(m_linkedCh), plot)
               + channelToX(static_cast<double>(m_linkedCh + 1), plot));
        const int xl = static_cast<int>(std::lround(xMidL));
        p.setPen(QPen(QColor(100, 220, 255, 200), 1, Qt::DotLine));
        p.drawLine(xl, plot.top(), xl, plot.bottom());
    }

    // Active hover cursor (or linked-only when mouse is not on spectrum).
    const int ch = (m_cursorCh >= 0) ? m_cursorCh : m_linkedCh;
    if (ch < 0 || ch >= n) {
        return;
    }

    const double xMid = 0.5
        * (channelToX(static_cast<double>(ch), plot)
           + channelToX(static_cast<double>(ch + 1), plot));
    const int x = static_cast<int>(std::lround(xMid));

    const bool fromLink = (m_cursorCh < 0 && m_linkedCh >= 0);
    p.setPen(QPen(fromLink ? QColor(100, 220, 255, 220) : QColor(255, 200, 80, 220), 1,
                  Qt::DashLine));
    p.drawLine(x, plot.top(), x, plot.bottom());

    const double e = channelToEnergy(static_cast<double>(ch));
    const quint32 c = m_counts[ch];
    QString text;
    if (hasEnergyAxis()) {
        text = tr("E = %1 keV  ·  ch %2  ·  N = %3")
                   .arg(e, 0, 'f', 1)
                   .arg(ch)
                   .arg(c);
    } else {
        text = tr("ch %1  ·  N = %2").arg(ch).arg(c);
    }
    if (fromLink) {
        text = tr("Linked · %1").arg(text);
    }

    const QFontMetrics fm(p.font());
    const int pad = 6;
    const int tw = fm.horizontalAdvance(text) + 2 * pad;
    const int th = fm.height() + 2 * pad;
    int bx = x + 8;
    int by = plot.top() + 8;
    if (bx + tw > plot.right() - 2) {
        bx = x - 8 - tw;
    }
    if (bx < plot.left() + 2) {
        bx = plot.left() + 2;
    }
    if (by + th > plot.bottom() - 2) {
        by = plot.bottom() - th - 2;
    }

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 22, 28, 220));
    p.drawRoundedRect(QRect(bx, by, tw, th), 4, 4);
    p.setPen(fromLink ? QColor(160, 230, 255) : QColor(255, 220, 140));
    p.drawText(bx + pad, by + pad + fm.ascent(), text);
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning && !m_counts.isEmpty()) {
        const QRect plot = plotRect();
        const int dx = event->pos().x() - m_lastPanPos.x();
        m_lastPanPos = event->pos();
        if (plot.width() > 0 && dx != 0) {
            const double span = m_xMax - m_xMin;
            const double dCh = -static_cast<double>(dx) / static_cast<double>(plot.width()) * span;
            m_xMin += dCh;
            m_xMax += dCh;
            clampView();
            emitViewRange();
            setCursorFromPos(event->pos());
            update();
        }
        event->accept();
        return;
    }
    setCursorFromPos(event->pos());
    event->accept();
}

void SpectrumWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && plotRect().contains(event->pos())
        && !m_counts.isEmpty()) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SpectrumWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SpectrumWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        resetView();
        setCursorFromPos(event->pos());
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SpectrumWidget::wheelEvent(QWheelEvent *event)
{
    if (m_counts.isEmpty()) {
        QWidget::wheelEvent(event);
        return;
    }
    const QRect plot = plotRect();
    const QPoint pos = event->position().toPoint();
    if (!plot.contains(pos)) {
        QWidget::wheelEvent(event);
        return;
    }

    const double anchor = xToChannel(pos.x(), plot);
    const double span = m_xMax - m_xMin;
    const double steps = event->angleDelta().y() / 120.0;
    if (std::fabs(steps) < 1e-6) {
        event->accept();
        return;
    }
    // Wheel up (positive) → zoom in.
    const double factor = std::pow(kZoomFactor, -steps);
    double newSpan = span * factor;
    const double nD = static_cast<double>(channelCount());
    newSpan = std::clamp(newSpan, kMinSpanChannels, nD);

    const double t = (span > 1e-12) ? (anchor - m_xMin) / span : 0.5;
    m_xMin = anchor - t * newSpan;
    m_xMax = m_xMin + newSpan;
    clampView();
    emitViewRange();
    setCursorFromPos(pos);
    update();
    event->accept();
}

void SpectrumWidget::leaveEvent(QEvent *event)
{
    if (!m_panning) {
        clearCursor();
    }
    QWidget::leaveEvent(event);
}
