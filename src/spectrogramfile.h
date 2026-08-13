#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QtGlobal>

// Binary spectrogram session format (.rcsg) — little-endian.
//
//   magic "RCSG" | version u16 | flags u16
//   nChannels u32 | nRows u32
//   a0,a1,a2 f32 | integrate u32 | historyMinutes u32
//   serialLen u32 | serial UTF-8
//   reserved u32 x 4
//   for each row:
//     wallTimeMs i64 | liveTimeSec u32 | intervalSec u32
//     rates f32[nChannels]
//     if flags&HasDeltas: deltas u32[nChannels]
//
// Rows are oldest → newest. Designed so a later continuous-writer can append
// rows after a fixed header (rewrite nRows on close, or use a trailer).

namespace SpectrogramFile {

inline constexpr char kMagic[4] = {'R', 'C', 'S', 'G'};
inline constexpr quint16 kVersion = 1;
inline constexpr quint16 kFlagHasDeltas = 1u;

struct Row {
    QVector<float> rates;
    QVector<quint32> deltas;
    quint32 liveTimeSec = 0;
    quint32 intervalSec = 0;
    QDateTime wallTime;
};

struct Document {
    quint32 nChannels = 0;
    float a0 = 0;
    float a1 = 0;
    float a2 = 0;
    quint32 integrate = 1;
    quint32 historyMinutes = 120;
    QString serial;
    QVector<Row> rows; // oldest first
};

/// Write full document (overwrite). Returns true on success.
bool save(const QString &path, const Document &doc, QString *errorMessage = nullptr);

/// Read document. Returns true on success.
bool load(const QString &path, Document *doc, QString *errorMessage = nullptr);

QString fileFilter();
QString defaultExtension();

} // namespace SpectrogramFile
