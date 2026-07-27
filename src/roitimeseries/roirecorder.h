#pragma once

#include "roitimeseries/roimath.h"

#include "protocol/types.h"

#include <QDateTime>
#include <QObject>
#include <QVector>

// One sample: ROI counts/rates at a spectrum timestamp.
struct RoiTimeSample {
    QDateTime hostTime;
    quint32 liveSec = 0;
    double elapsedFromT0 = 0; // seconds; NaN if t0 not set
    double grossCps = 0;
    quint64 grossCounts = 0;
    QVector<RoiSample> rois;
};

// Accumulates spectrum-based ROI rates for time-series runs.
class RoiTimeSeriesRecorder : public QObject {
    Q_OBJECT
public:
    explicit RoiTimeSeriesRecorder(QObject *parent = nullptr);

    void setRois(const QVector<RoiWindow> &rois);
    QVector<RoiWindow> rois() const { return m_rois; }

    void clear();
    void setT0(const QDateTime &t0);
    void clearT0();
    bool hasT0() const { return m_hasT0; }
    QDateTime t0() const { return m_t0; }

    bool ingestSpectrum(const QtRadiacode::RcSpectrum &sp);

    const QVector<RoiTimeSample> &samples() const { return m_samples; }
    int sampleCount() const { return m_samples.size(); }

    float lastA0() const { return m_lastA0; }
    float lastA1() const { return m_lastA1; }
    float lastA2() const { return m_lastA2; }

signals:
    void sampleAdded(const RoiTimeSample &sample);
    void samplesCleared();

private:
    QVector<RoiWindow> m_rois;
    QVector<RoiTimeSample> m_samples;

    bool m_hasPrev = false;
    quint32 m_prevLive = 0;
    quint64 m_prevGross = 0;
    QVector<quint64> m_prevRoiCounts;

    bool m_hasT0 = false;
    QDateTime m_t0;

    float m_lastA0 = 0;
    float m_lastA1 = 0;
    float m_lastA2 = 0;
};
