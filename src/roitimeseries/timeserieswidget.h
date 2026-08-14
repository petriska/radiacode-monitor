#pragma once

#include "roitimeseries/roirecorder.h"

#include <QColor>
#include <QHash>
#include <QVector>
#include <QWidget>

// Multi-series strip chart for ROI cps vs elapsed time (or sample index).
class TimeSeriesWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimeSeriesWidget(QWidget *parent = nullptr);

    /// colorOverrides: optional per-series id → colour (e.g. "gross", "selection").
    void setSamples(const QVector<RoiTimeSample> &samples, bool hasT0, const QDateTime &t0,
                    const QVector<RoiWindow> &roiOrder = {},
                    const QHash<QString, QColor> &colorOverrides = {});
    /// When true, Y uses log(1+y) so zeros stay valid; otherwise linear.
    void setLogYScale(bool on);
    bool logYScale() const { return m_logY; }
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Series {
        QString id;
        QString label;
        QColor color;
        QVector<QPointF> points;
    };

    double yNorm(double y) const;

    QVector<Series> m_series;
    bool m_hasT0 = false;
    double m_t0Elapsed = 0;
    bool m_useElapsed = false;
    double m_xMin = 0;
    double m_xMax = 1;
    double m_yMax = 1;
    bool m_logY = false;
};
