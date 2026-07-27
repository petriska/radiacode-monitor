#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

// Energy-window ROI and helpers for spectrum integration.
struct RoiWindow {
    QString id;
    QString name;
    double eMinKeV = 0;
    double eMaxKeV = 0;
    bool enabled = true;
};

struct RoiSample {
    QString id;
    quint64 counts = 0;
    double cps = 0; // incremental or N/T depending on recorder mode
};

struct RoiPreset {
    QString id;
    QString name;
    QVector<RoiWindow> rois;
};

// Built-in presets (radon daughters, empty custom, …).
QVector<RoiPreset> roiPresets();
RoiPreset presetById(const QString &id);
QVector<RoiWindow> presetRadonDaughters();

// E(ch) = a0 + a1*ch + a2*ch^2 — find channel whose energy is closest to keV in [0, n).
int energyToChannel(double keV, float a0, float a1, float a2, int nChannels);

// Inclusive channel range covering [eMin, eMax] given calibration. Returns false if invalid.
bool roiToChannels(const RoiWindow &roi, float a0, float a1, float a2, int nChannels,
                   int *chMinOut, int *chMaxOut);

quint64 sumChannels(const QVector<quint32> &counts, int chMin, int chMax);

bool hasEnergyCalibration(float a0, float a1, float a2);

// Stable id from name for CSV columns.
QString makeRoiId(const QString &name);
