#pragma once

#include "roitimeseries/roimath.h"
#include "roitimeseries/roirecorder.h"

#include <QString>
#include <QVector>

namespace RoiTimeSeriesExport {

// Returns empty QString on success, or an error message.
QString writeCsv(const QString &path,
                 const QVector<RoiTimeSample> &samples,
                 const QVector<RoiWindow> &rois,
                 const QString &serial,
                 const QDateTime &t0,
                 bool hasT0,
                 float a0, float a1, float a2);

} // namespace RoiTimeSeriesExport
