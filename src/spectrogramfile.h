#pragma once

#include <QDateTime>
#include <QFile>
#include <QString>
#include <QVector>
#include <QtGlobal>

// Binary spectrogram session format (.rcsg) — little-endian.
//
//   magic "RCSG" | version u16 | flags u16
//   nChannels u32 | nRows u32          ← nRows at fixed offset 12 (for append)
//   a0,a1,a2 f32 | integrate u32 | historyMinutes u32
//   serialLen u32 | serial UTF-8
//   reserved u32 x 4
//   for each row:
//     wallTimeMs i64 | liveTimeSec u32 | intervalSec u32
//     rates f32[nChannels]
//     if flags&HasDeltas: deltas u32[nChannels]
//
// Rows are oldest → newest. Continuous recording appends rows and patches nRows.

namespace SpectrogramFile {

inline constexpr char kMagic[4] = {'R', 'C', 'S', 'G'};
inline constexpr quint16 kVersion = 1;
inline constexpr quint16 kFlagHasDeltas = 1u;
/// Byte offset of nRows (uint32 LE) from start of file.
inline constexpr qint64 kNRowsFieldOffset = 12;

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

struct Header {
    quint32 nChannels = 0;
    float a0 = 0;
    float a1 = 0;
    float a2 = 0;
    quint32 integrate = 1;
    quint32 historyMinutes = 120;
    QString serial;
    quint16 flags = kFlagHasDeltas;
};

/// Write full document (overwrite). Returns true on success.
bool save(const QString &path, const Document &doc, QString *errorMessage = nullptr);

/// Read document. Returns true on success.
bool load(const QString &path, Document *doc, QString *errorMessage = nullptr);

/// Bytes per data row (header fields + rates + optional deltas).
qint64 rowByteSize(quint32 nChannels, bool hasDeltas = true);

QString fileFilter();
QString defaultExtension();

/// Append-friendly writer for continuous recording.
class AppendWriter {
public:
    AppendWriter() = default;
    ~AppendWriter();

    AppendWriter(const AppendWriter &) = delete;
    AppendWriter &operator=(const AppendWriter &) = delete;

    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_path; }
    quint32 nChannels() const { return m_nChannels; }
    quint32 nRows() const { return m_nRows; }
    Header header() const { return m_header; }

    /// Create a new file with 0 rows (overwrite if exists).
    bool create(const QString &path, const Header &header, QString *errorMessage = nullptr);

    /// Open existing .rcsg for append (must match channel count when appending).
    bool openForAppend(const QString &path, QString *errorMessage = nullptr);

    bool appendRow(const Row &row, QString *errorMessage = nullptr);
    bool flush(QString *errorMessage = nullptr);
    /// Patch nRows and close.
    bool close(QString *errorMessage = nullptr);

private:
    bool writeHeader(const Header &header, quint32 nRows, QString *errorMessage);
    bool patchNRows(QString *errorMessage);
    bool writeRowPayload(const Row &row, QString *errorMessage);

    QFile m_file;
    QString m_path;
    Header m_header;
    quint32 m_nChannels = 0;
    quint32 m_nRows = 0;
    bool m_hasDeltas = true;
    qint64 m_dataStart = 0;
};

} // namespace SpectrogramFile
