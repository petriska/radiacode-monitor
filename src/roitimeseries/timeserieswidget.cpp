#include "roitimeseries/timeserieswidget.h"

#include "roitimeseries/roimath.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

TimeSeriesWidget::TimeSeriesWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TimeSeriesWidget::clear()
{
    m_series.clear();
    m_hasT0 = false;
    m_useElapsed = false;
    m_xMin = 0;
    m_xMax = 1;
    m_yMax = 1;
    update();
}

void TimeSeriesWidget::setSamples(const QVector<RoiTimeSample> &samples, bool hasT0,
                                  const QDateTime &t0, const QVector<RoiWindow> &roiOrder)
{
    Q_UNUSED(t0);
    m_series.clear();
    m_hasT0 = hasT0;
    m_useElapsed = false;
    m_xMin = 0;
    m_xMax = 1;
    m_yMax = 1;

    if (samples.isEmpty()) {
        update();
        return;
    }

    QStringList ids;
    for (const RoiTimeSample &s : samples) {
        for (const RoiSample &r : s.rois) {
            if (!ids.contains(r.id)) {
                ids.append(r.id);
            }
        }
    }
    ids.prepend(QStringLiteral("gross"));

    auto colorForId = [&](const QString &id) -> QColor {
        if (id == QStringLiteral("gross")) {
            return roiSeriesColor(0);
        }
        for (int i = 0; i < roiOrder.size(); ++i) {
            if (roiOrder[i].id == id) {
                return roiSeriesColor(i + 1);
            }
        }
        return roiSeriesColor(1);
    };

    for (const QString &id : ids) {
        Series ser;
        ser.id = id;
        ser.label = (id == QStringLiteral("gross")) ? QStringLiteral("gross") : id;
        ser.color = colorForId(id);
        m_series.append(ser);
    }

    int finiteElapsed = 0;
    for (const RoiTimeSample &s : samples) {
        if (std::isfinite(s.elapsedFromT0)) {
            ++finiteElapsed;
        }
    }
    m_useElapsed = hasT0 && finiteElapsed > 0;

    double xMin = 0;
    double xMax = 1;
    double yMax = 1e-9;
    bool firstX = true;

    for (int si = 0; si < samples.size(); ++si) {
        const RoiTimeSample &s = samples[si];
        const double x = m_useElapsed ? s.elapsedFromT0 : static_cast<double>(si);
        if (!std::isfinite(x)) {
            continue;
        }
        if (firstX) {
            xMin = xMax = x;
            firstX = false;
        } else {
            xMin = std::min(xMin, x);
            xMax = std::max(xMax, x);
        }

        for (Series &ser : m_series) {
            double y = 0;
            if (ser.id == QStringLiteral("gross")) {
                y = s.grossCps;
            } else {
                for (const RoiSample &r : s.rois) {
                    if (r.id == ser.id) {
                        y = r.cps;
                        break;
                    }
                }
            }
            if (std::isfinite(y)) {
                ser.points.append(QPointF(x, y));
                yMax = std::max(yMax, y);
            }
        }
    }

    if (firstX) {
        m_xMin = 0;
        m_xMax = 1;
    } else {
        m_xMin = xMin;
        m_xMax = (xMax > xMin) ? xMax : (xMin + 1.0);
    }
    m_yMax = yMax * 1.1;
    if (m_yMax <= 0) {
        m_yMax = 1;
    }
    m_t0Elapsed = 0;
    update();
}

void TimeSeriesWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 26, 30));

    const int left = 52;
    const int right = 12;
    const int top = 12;
    const int bottom = 40;
    const QRect plot(left, top, std::max(1, width() - left - right),
                     std::max(1, height() - top - bottom));

    p.setPen(QColor(55, 58, 64));
    p.setBrush(QColor(30, 32, 38));
    p.drawRect(plot);

    if (m_series.isEmpty() || m_series.first().points.isEmpty()) {
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter, tr("No samples yet — start recording"));
        return;
    }

    const double xSpan = std::max(1e-9, m_xMax - m_xMin);
    auto toX = [&](double x) {
        return plot.left() + (x - m_xMin) / xSpan * plot.width();
    };
    auto toY = [&](double y) {
        const double t = std::clamp(y / m_yMax, 0.0, 1.0);
        return plot.bottom() - t * (plot.height() - 1);
    };

    p.setPen(QColor(45, 48, 54));
    for (int i = 0; i <= 4; ++i) {
        const int y = plot.top() + i * plot.height() / 4;
        p.drawLine(plot.left(), y, plot.right(), y);
    }

    if (m_useElapsed && m_hasT0 && m_t0Elapsed >= m_xMin && m_t0Elapsed <= m_xMax) {
        const int x0 = static_cast<int>(std::lround(toX(m_t0Elapsed)));
        p.setPen(QPen(QColor(255, 100, 100, 200), 1, Qt::DashLine));
        p.drawLine(x0, plot.top(), x0, plot.bottom());
        p.setPen(QColor(255, 140, 140));
        p.drawText(x0 + 4, plot.top() + 14, tr("t₀"));
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    for (const Series &ser : m_series) {
        if (ser.points.size() < 2) {
            if (ser.points.size() == 1) {
                p.setPen(QPen(ser.color, 2));
                const QPointF &pt = ser.points.first();
                p.drawEllipse(QPointF(toX(pt.x()), toY(pt.y())), 3, 3);
            }
            continue;
        }
        p.setPen(QPen(ser.color, 1.5));
        QPainterPath path;
        bool started = false;
        for (const QPointF &pt : ser.points) {
            const QPointF q(toX(pt.x()), toY(pt.y()));
            if (!started) {
                path.moveTo(q);
                started = true;
            } else {
                path.lineTo(q);
            }
        }
        p.drawPath(path);
    }

    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QColor(170, 172, 180));
    p.drawText(4, plot.top() + 12, QString::number(m_yMax, 'g', 3));
    p.drawText(4, plot.bottom(), QStringLiteral("0"));
    const QString xLabel = m_useElapsed ? tr("elapsed from t₀ (s)") : tr("sample #");
    p.drawText(plot.center().x() - 50, height() - 6, xLabel);
    p.drawText(plot.left(), height() - 6, QString::number(m_xMin, 'f', 0));
    p.drawText(plot.right() - 40, height() - 6, QString::number(m_xMax, 'f', 0));

    int lx = plot.left() + 8;
    int ly = plot.top() + 8;
    for (const Series &ser : m_series) {
        p.setPen(ser.color);
        p.drawText(lx, ly + 10, ser.label);
        ly += 14;
        if (ly > plot.top() + 80) {
            break;
        }
    }
}
