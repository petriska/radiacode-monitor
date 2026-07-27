#pragma once

#include "roitimeseries/roirecorder.h"

#include <QColor>
#include <QVector>
#include <QWidget>

// Multi-series strip chart for ROI cps vs elapsed time (or sample index).
class TimeSeriesWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimeSeriesWidget(QWidget *parent = nullptr);

    void setSamples(const QVector<RoiTimeSample> &samples, bool hasT0, const QDateTime &t0);
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

    QVector<Series> m_series;
    bool m_hasT0 = false;
    double m_t0Elapsed = 0;
    bool m_useElapsed = false;
    double m_xMin = 0;
    double m_xMax = 1;
    double m_yMax = 1;
};
