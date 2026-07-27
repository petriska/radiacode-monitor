#include "roitimeseries/roirecorder.h"

#include <cmath>
#include <limits>

RoiTimeSeriesRecorder::RoiTimeSeriesRecorder(QObject *parent)
    : QObject(parent)
    , m_rois(presetRadonDaughters())
{
}

void RoiTimeSeriesRecorder::setRois(const QVector<RoiWindow> &rois)
{
    m_rois = rois;
}

void RoiTimeSeriesRecorder::clear()
{
    m_samples.clear();
    m_hasPrev = false;
    m_prevLive = 0;
    m_prevGross = 0;
    m_prevRoiCounts.clear();
    emit samplesCleared();
}

void RoiTimeSeriesRecorder::setT0(const QDateTime &t0)
{
    m_hasT0 = t0.isValid();
    m_t0 = t0;
    for (auto &s : m_samples) {
        if (m_hasT0) {
            s.elapsedFromT0 = m_t0.msecsTo(s.hostTime) / 1000.0;
        } else {
            s.elapsedFromT0 = std::numeric_limits<double>::quiet_NaN();
        }
    }
}

void RoiTimeSeriesRecorder::clearT0()
{
    m_hasT0 = false;
    m_t0 = {};
    for (auto &s : m_samples) {
        s.elapsedFromT0 = std::numeric_limits<double>::quiet_NaN();
    }
}

bool RoiTimeSeriesRecorder::ingestSpectrum(const QtRadiacode::RcSpectrum &sp)
{
    if (sp.counts.isEmpty()) {
        return false;
    }

    m_lastA0 = sp.a0;
    m_lastA1 = sp.a1;
    m_lastA2 = sp.a2;

    const int n = sp.counts.size();
    quint64 gross = 0;
    for (quint32 c : sp.counts) {
        gross += c;
    }

    QVector<RoiSample> roiSamples;
    QVector<quint64> roiCounts;
    roiSamples.reserve(m_rois.size());
    roiCounts.reserve(m_rois.size());

    for (const RoiWindow &roi : m_rois) {
        if (!roi.enabled) {
            continue;
        }
        RoiSample rs;
        rs.id = roi.id;
        int c0 = 0;
        int c1 = 0;
        if (roiToChannels(roi, sp.a0, sp.a1, sp.a2, n, &c0, &c1)) {
            rs.counts = sumChannels(sp.counts, c0, c1);
        } else {
            rs.counts = 0;
        }
        rs.cps = 0;
        roiSamples.append(rs);
        roiCounts.append(rs.counts);
    }

    RoiTimeSample sample;
    sample.hostTime = QDateTime::currentDateTimeUtc();
    sample.liveSec = sp.durationSec;
    sample.grossCounts = gross;
    if (m_hasT0) {
        sample.elapsedFromT0 = m_t0.msecsTo(sample.hostTime) / 1000.0;
    } else {
        sample.elapsedFromT0 = std::numeric_limits<double>::quiet_NaN();
    }

    if (m_hasPrev && sp.durationSec > m_prevLive) {
        const double dt = static_cast<double>(sp.durationSec - m_prevLive);
        sample.grossCps = static_cast<double>(gross - m_prevGross) / dt;
        for (int i = 0; i < roiSamples.size(); ++i) {
            const quint64 dN = (roiCounts[i] >= m_prevRoiCounts.value(i))
                ? (roiCounts[i] - m_prevRoiCounts.value(i))
                : 0;
            roiSamples[i].cps = static_cast<double>(dN) / dt;
        }
    } else if (sp.durationSec > 0) {
        const double t = static_cast<double>(sp.durationSec);
        sample.grossCps = static_cast<double>(gross) / t;
        for (int i = 0; i < roiSamples.size(); ++i) {
            roiSamples[i].cps = static_cast<double>(roiCounts[i]) / t;
        }
    }

    sample.rois = roiSamples;
    m_samples.append(sample);

    m_hasPrev = true;
    m_prevLive = sp.durationSec;
    m_prevGross = gross;
    m_prevRoiCounts = roiCounts;

    emit sampleAdded(sample);
    return true;
}
