#include "spectrumwidget.h"

#include <QPainter>
#include <QtMath>
#include <algorithm>

SpectrumWidget::SpectrumWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SpectrumWidget::setSpectrum(const QVector<quint32> &counts, float a0, float a1, float a2)
{
    m_counts = counts;
    m_a0 = a0;
    m_a1 = a1;
    m_a2 = a2;
    update();
}

void SpectrumWidget::clear()
{
    m_counts.clear();
    update();
}

void SpectrumWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 26, 30));
    p.setRenderHint(QPainter::Antialiasing, false);

    if (m_counts.isEmpty()) {
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter, tr("No spectrum yet"));
        return;
    }

    const int n = m_counts.size();
    quint32 maxC = 1;
    for (quint32 c : m_counts) {
        maxC = std::max(maxC, c);
    }

    const int left = 48;
    const int right = 12;
    const int top = 12;
    const int bottom = 28;
    const QRect plot(left, top, width() - left - right, height() - top - bottom);

    p.setPen(QColor(70, 70, 75));
    p.drawRect(plot);

    // Log-ish vertical scale for sparse spectra (sqrt for readability).
    p.setPen(QColor(90, 180, 255));
    for (int i = 0; i < n; ++i) {
        const double t = qSqrt(double(m_counts[i]) / double(maxC));
        const int x0 = plot.left() + (i * plot.width()) / n;
        const int x1 = plot.left() + ((i + 1) * plot.width()) / n;
        const int h = int(t * (plot.height() - 1));
        p.fillRect(x0, plot.bottom() - h, std::max(1, x1 - x0), h, QColor(70, 150, 255, 200));
    }

    p.setPen(QColor(180, 180, 180));
    p.drawText(4, plot.center().y(), QString::number(maxC));
    p.drawText(plot.left(), height() - 8, QStringLiteral("ch 0"));
    p.drawText(plot.right() - 40, height() - 8, QStringLiteral("ch %1").arg(n - 1));
    if (m_a1 != 0.0f || m_a2 != 0.0f) {
        p.drawText(plot.center().x() - 40, height() - 8,
                   QStringLiteral("~keV calib a0=%1").arg(m_a0, 0, 'f', 1));
    }
}
