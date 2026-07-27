#include "roitimeseries/roimath.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

QColor roiSeriesColor(int index)
{
    static const QColor kColors[] = {
        QColor(80, 180, 255),
        QColor(255, 180, 70),
        QColor(120, 220, 140),
        QColor(220, 120, 200),
        QColor(200, 200, 120),
        QColor(255, 120, 120),
        QColor(160, 140, 255),
    };
    const int n = int(sizeof(kColors) / sizeof(kColors[0]));
    return kColors[(index % n + n) % n];
}

QVector<RoiWindow> presetRadonDaughters()
{
    return {
        {QStringLiteral("pb214_295"), QStringLiteral("214Pb 295 keV"), 285.0, 305.0, true},
        {QStringLiteral("pb214_352"), QStringLiteral("214Pb 352 keV"), 340.0, 365.0, true},
        {QStringLiteral("bi214_609"), QStringLiteral("214Bi 609 keV"), 590.0, 630.0, true},
    };
}

QVector<RoiPreset> roiPresets()
{
    return {
        {QStringLiteral("radon_daughters"),
         QStringLiteral("Radon daughters (214Pb / 214Bi)"),
         presetRadonDaughters()},
        {QStringLiteral("empty"), QStringLiteral("Empty (custom ROIs)"), {}},
    };
}

RoiPreset presetById(const QString &id)
{
    for (const RoiPreset &p : roiPresets()) {
        if (p.id == id) {
            return p;
        }
    }
    return roiPresets().constFirst();
}

QString makeRoiId(const QString &name)
{
    QString s = name.trimmed().toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    s.replace(QRegularExpression(QStringLiteral("^_+|_+$")), QString());
    if (s.isEmpty()) {
        s = QStringLiteral("roi");
    }
    return s;
}

bool hasEnergyCalibration(float a0, float a1, float a2)
{
    Q_UNUSED(a0);
    return std::fabs(static_cast<double>(a1)) > 1e-12
        || std::fabs(static_cast<double>(a2)) > 1e-12;
}

static double channelEnergy(double ch, float a0, float a1, float a2)
{
    return static_cast<double>(a0) + static_cast<double>(a1) * ch
        + static_cast<double>(a2) * ch * ch;
}

int energyToChannel(double keV, float a0, float a1, float a2, int nChannels)
{
    if (nChannels <= 0) {
        return 0;
    }
    int best = 0;
    double bestErr = std::fabs(channelEnergy(0.0, a0, a1, a2) - keV);
    for (int ch = 1; ch < nChannels; ++ch) {
        const double e = channelEnergy(static_cast<double>(ch), a0, a1, a2);
        const double err = std::fabs(e - keV);
        if (err < bestErr) {
            bestErr = err;
            best = ch;
        }
    }
    return best;
}

bool roiToChannels(const RoiWindow &roi, float a0, float a1, float a2, int nChannels,
                   int *chMinOut, int *chMaxOut)
{
    if (!chMinOut || !chMaxOut || nChannels <= 0 || roi.eMaxKeV <= roi.eMinKeV) {
        return false;
    }
    if (!hasEnergyCalibration(a0, a1, a2)) {
        return false;
    }
    int c0 = energyToChannel(roi.eMinKeV, a0, a1, a2, nChannels);
    int c1 = energyToChannel(roi.eMaxKeV, a0, a1, a2, nChannels);
    if (c1 < c0) {
        std::swap(c0, c1);
    }
    c0 = std::clamp(c0, 0, nChannels - 1);
    c1 = std::clamp(c1, 0, nChannels - 1);
    *chMinOut = c0;
    *chMaxOut = c1;
    return c1 >= c0;
}

quint64 sumChannels(const QVector<quint32> &counts, int chMin, int chMax)
{
    if (counts.isEmpty()) {
        return 0;
    }
    const int n = counts.size();
    chMin = std::clamp(chMin, 0, n - 1);
    chMax = std::clamp(chMax, 0, n - 1);
    if (chMax < chMin) {
        std::swap(chMin, chMax);
    }
    quint64 sum = 0;
    for (int i = chMin; i <= chMax; ++i) {
        sum += counts[i];
    }
    return sum;
}
