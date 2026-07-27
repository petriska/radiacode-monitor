#include "roitimeseries/roiexport.h"

#include <QFile>
#include <QTextStream>

#include <cmath>

namespace RoiTimeSeriesExport {

QString writeCsv(const QString &path,
                 const QVector<RoiTimeSample> &samples,
                 const QVector<RoiWindow> &rois,
                 const QString &serial,
                 const QDateTime &t0,
                 bool hasT0,
                 float a0, float a1, float a2)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return QObject::tr("Cannot open file for writing: %1").arg(path);
    }
    QTextStream out(&f);
    out.setRealNumberNotation(QTextStream::SmartNotation);
    out.setRealNumberPrecision(8);

    out << "# radiacode-monitor ROI time series\n";
    out << "# serial=" << serial << "\n";
    if (hasT0 && t0.isValid()) {
        out << "# t0=" << t0.toUTC().toString(Qt::ISODateWithMs) << "\n";
    } else {
        out << "# t0=(not set)\n";
    }
    out << "# calib a0=" << a0 << " a1=" << a1 << " a2=" << a2 << "\n";
    for (const RoiWindow &r : rois) {
        if (!r.enabled) {
            continue;
        }
        out << "# roi " << r.id << " name=\"" << r.name << "\" "
            << r.eMinKeV << "-" << r.eMaxKeV << " keV\n";
    }
    out << "# rate_note=incremental cps between spectra when live time increases; "
           "else N/live_s after reset\n";

    out << "timestamp_iso,elapsed_s,live_s,gross_counts,gross_cps";
    for (const RoiWindow &r : rois) {
        if (!r.enabled) {
            continue;
        }
        out << "," << r.id << "_counts," << r.id << "_cps";
    }
    out << "\n";

    for (const RoiTimeSample &s : samples) {
        out << s.hostTime.toUTC().toString(Qt::ISODateWithMs) << ",";
        if (std::isfinite(s.elapsedFromT0)) {
            out << s.elapsedFromT0;
        }
        out << "," << s.liveSec << "," << s.grossCounts << "," << s.grossCps;

        for (const RoiWindow &r : rois) {
            if (!r.enabled) {
                continue;
            }
            const RoiSample *found = nullptr;
            for (const RoiSample &rs : s.rois) {
                if (rs.id == r.id) {
                    found = &rs;
                    break;
                }
            }
            if (found) {
                out << "," << found->counts << "," << found->cps;
            } else {
                out << ",,";
            }
        }
        out << "\n";
    }

    return {};
}

} // namespace RoiTimeSeriesExport
