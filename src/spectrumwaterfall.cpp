#include "spectrumwaterfall.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>

SpectrumWaterfall::SpectrumWaterfall(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setToolTip(tr(
        "Waterfall: count rate per channel over time (ΔN / Δt between spectra).\n"
        "Newest row at the bottom. Hover for energy, rate, ΔN, and time.\n"
        "X range follows spectrum zoom when linked."));
}

void SpectrumWaterfall::setMaxRows(int rows)
{
    m_maxRows = qBound(32, rows, 2000);
    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
    }
    if (m_cursorRow >= m_rows.size()) {
        clearCursor();
    }
    rebuildImage();
    update();
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
    m_prevCounts.clear();
    m_prevDurationSec = 0;
    m_havePrev = false;
    m_channels = 0;
    m_displayMax = 1.0f;
    m_image = QImage();
    m_linkedCh = -1;
    clearCursor();
    update();
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

void SpectrumWaterfall::appendRow(Row &&row)
{
    float rowMax = 0.0f;
    for (float v : row.rates) {
        rowMax = std::max(rowMax, v);
    }
    m_displayMax = std::max(rowMax, m_displayMax * 0.992f);
    if (m_displayMax < 1e-6f) {
        m_displayMax = 1e-6f;
    }

    m_rows.append(std::move(row));
    while (m_rows.size() > m_maxRows) {
        m_rows.removeFirst();
        if (m_cursorRow > 0) {
            --m_cursorRow;
        } else if (m_cursorRow == 0) {
            clearCursor();
        }
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
        m_rows.clear();
        m_channels = n;
        m_havePrev = false;
        m_prevCounts.clear();
        clearCursor();
        if (m_xMax <= m_xMin || m_xMax > n) {
            m_xMin = 0;
            m_xMax = n;
        }
    }

    if (!m_havePrev || m_prevCounts.size() != n) {
        m_prevCounts = counts;
        m_prevDurationSec = durationSec;
        m_havePrev = true;
        return;
    }

    const qint64 dT = qint64(durationSec) - qint64(m_prevDurationSec);
    if (dT <= 0) {
        m_prevCounts = counts;
        m_prevDurationSec = durationSec;
        return;
    }

    Row row;
    row.rates.resize(n);
    row.deltas.resize(n);
    row.liveTimeSec = durationSec;
    row.intervalSec = static_cast<quint32>(dT);
    row.wallTime = QDateTime::currentDateTime();
    for (int i = 0; i < n; ++i) {
        const qint64 dN = qint64(counts[i]) - qint64(m_prevCounts[i]);
        const quint32 dNu = dN > 0 ? static_cast<quint32>(dN) : 0u;
        row.deltas[i] = dNu;
        row.rates[i] = float(dNu) / float(dT);
    }

    m_prevCounts = counts;
    m_prevDurationSec = durationSec;
    appendRow(std::move(row));
}

int SpectrumWaterfall::ageFromNewestSec(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= m_rows.size()) {
        return 0;
    }
    // Sum intervals of rows after this one (toward newest).
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
    const int nRows = m_rows.size();
    if (nRows <= 0 || m_image.isNull() || m_image.height() <= 0) {
        return -1;
    }
    // Image is drawn into plot.adjusted(1,1,-1,-1); map Y → image row → m_rows index.
    const QRect imgRect = plot.adjusted(1, 1, -1, -1);
    if (y < imgRect.top() || y > imgRect.bottom()) {
        return -1;
    }
    const int h = std::max(1, imgRect.height());
    const double ty = (double(y) - imgRect.top()) / double(std::max(1, h - 1));
    const int imgY =
        std::clamp(int(std::lround(ty * (m_image.height() - 1))), 0, m_image.height() - 1);
    // Image: empty at top (y0 = maxRows - nRows), data rows at bottom.
    const int y0 = m_maxRows - nRows;
    const int dataY = imgY - y0;
    if (dataY < 0 || dataY >= nRows) {
        return -1;
    }
    return dataY;
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

    // Linked vertical from spectrum (cyan), full height.
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
        const int nRows = m_rows.size();
        const int y0img = m_maxRows - nRows;
        const int imgY = y0img + m_cursorRow;
        const double ty = m_image.height() > 1
            ? double(imgY) / double(m_image.height() - 1)
            : 0.0;
        y = imgRect.top() + int(std::lround(ty * (imgRect.height() - 1)));
        p.drawLine(plot.left(), y, plot.right(), y);
    }

    // Tooltip: hover uses that row; linked-only uses newest row for rate at channel.
    const int rowIdx = hover ? m_cursorRow : (m_rows.isEmpty() ? -1 : m_rows.size() - 1);
    if (rowIdx < 0) {
        return;
    }
    const Row &r = m_rows[rowIdx];
    const float rate = (ch < r.rates.size()) ? r.rates[ch] : 0.0f;
    const quint32 dN = (ch < r.deltas.size()) ? r.deltas[ch] : 0u;
    const int age = ageFromNewestSec(rowIdx);
    const double e = channelToEnergy(double(ch));

    QString text;
    if (hasEnergyAxis()) {
        text = tr("E = %1 keV · ch %2 · %3 cps · ΔN = %4 · live %5 s · t−%6 s")
                   .arg(e, 0, 'f', 1)
                   .arg(ch)
                   .arg(rate, 0, 'f', 2)
                   .arg(dN)
                   .arg(r.liveTimeSec)
                   .arg(age);
    } else {
        text = tr("ch %1 · %2 cps · ΔN = %3 · live %4 s · t−%5 s")
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

    const QRectF src(x0, 0.0, x1 - x0, double(m_image.height()));
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(plot.adjusted(1, 1, -1, -1), m_image, src);

    p.setPen(QColor(130, 132, 140));
    const QFontMetrics fm = p.fontMetrics();
    p.save();
    const QString yTitle = tr("Time →");
    p.translate(12, plot.center().y() + fm.horizontalAdvance(yTitle) / 2);
    p.rotate(-90);
    p.drawText(0, 0, yTitle);
    p.restore();

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
