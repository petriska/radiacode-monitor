#include "spectrogramfile.h"

#include <QFile>
#include <QDataStream>
#include <QIODevice>

namespace SpectrogramFile {
namespace {

void setError(QString *errorMessage, const QString &msg)
{
    if (errorMessage) {
        *errorMessage = msg;
    }
}

bool writeU16(QDataStream &ds, quint16 v)
{
    ds << v;
    return ds.status() == QDataStream::Ok;
}

bool writeU32(QDataStream &ds, quint32 v)
{
    ds << v;
    return ds.status() == QDataStream::Ok;
}

bool writeI64(QDataStream &ds, qint64 v)
{
    ds << v;
    return ds.status() == QDataStream::Ok;
}

bool writeF32(QDataStream &ds, float v)
{
    ds << v;
    return ds.status() == QDataStream::Ok;
}

} // namespace

QString fileFilter()
{
    return QStringLiteral("Spectrogram (*.rcsg);;All files (*.*)");
}

QString defaultExtension()
{
    return QStringLiteral("rcsg");
}

bool save(const QString &path, const Document &doc, QString *errorMessage)
{
    if (doc.nChannels == 0 || doc.rows.isEmpty()) {
        setError(errorMessage, QStringLiteral("No spectrogram rows to save."));
        return false;
    }
    for (const Row &r : doc.rows) {
        if (r.rates.size() != int(doc.nChannels)) {
            setError(errorMessage, QStringLiteral("Row channel count mismatch."));
            return false;
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(errorMessage, f.errorString());
        return false;
    }

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
    ds.setVersion(QDataStream::Qt_5_15);

    // Magic
    if (f.write(kMagic, 4) != 4) {
        setError(errorMessage, QStringLiteral("Failed to write magic."));
        return false;
    }

    const quint16 flags = kFlagHasDeltas;
    if (!writeU16(ds, kVersion) || !writeU16(ds, flags)) {
        setError(errorMessage, QStringLiteral("Failed to write version/flags."));
        return false;
    }

    const quint32 nRows = quint32(doc.rows.size());
    if (!writeU32(ds, doc.nChannels) || !writeU32(ds, nRows)) {
        setError(errorMessage, QStringLiteral("Failed to write dimensions."));
        return false;
    }
    if (!writeF32(ds, doc.a0) || !writeF32(ds, doc.a1) || !writeF32(ds, doc.a2)) {
        setError(errorMessage, QStringLiteral("Failed to write calibration."));
        return false;
    }
    if (!writeU32(ds, doc.integrate) || !writeU32(ds, doc.historyMinutes)) {
        setError(errorMessage, QStringLiteral("Failed to write integrate/history."));
        return false;
    }

    const QByteArray serialUtf8 = doc.serial.toUtf8();
    if (!writeU32(ds, quint32(serialUtf8.size()))) {
        setError(errorMessage, QStringLiteral("Failed to write serial length."));
        return false;
    }
    if (!serialUtf8.isEmpty() && f.write(serialUtf8) != serialUtf8.size()) {
        setError(errorMessage, QStringLiteral("Failed to write serial."));
        return false;
    }

    // Reserved for future header fields (16 bytes).
    for (int i = 0; i < 4; ++i) {
        if (!writeU32(ds, 0)) {
            setError(errorMessage, QStringLiteral("Failed to write reserved header."));
            return false;
        }
    }

    for (const Row &row : doc.rows) {
        const qint64 wallMs = row.wallTime.isValid()
                                  ? row.wallTime.toMSecsSinceEpoch()
                                  : qint64(0);
        if (!writeI64(ds, wallMs) || !writeU32(ds, row.liveTimeSec)
            || !writeU32(ds, row.intervalSec)) {
            setError(errorMessage, QStringLiteral("Failed to write row header."));
            return false;
        }
        for (int ch = 0; ch < int(doc.nChannels); ++ch) {
            if (!writeF32(ds, row.rates.at(ch))) {
                setError(errorMessage, QStringLiteral("Failed to write rates."));
                return false;
            }
        }
        for (int ch = 0; ch < int(doc.nChannels); ++ch) {
            const quint32 dN = (ch < row.deltas.size()) ? row.deltas.at(ch) : 0u;
            if (!writeU32(ds, dN)) {
                setError(errorMessage, QStringLiteral("Failed to write deltas."));
                return false;
            }
        }
    }

    if (ds.status() != QDataStream::Ok) {
        setError(errorMessage, QStringLiteral("Stream error while writing."));
        return false;
    }
    return true;
}

bool load(const QString &path, Document *doc, QString *errorMessage)
{
    if (!doc) {
        setError(errorMessage, QStringLiteral("Null document."));
        return false;
    }
    *doc = Document{};

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setError(errorMessage, f.errorString());
        return false;
    }

    char magic[4];
    if (f.read(magic, 4) != 4
        || magic[0] != kMagic[0] || magic[1] != kMagic[1]
        || magic[2] != kMagic[2] || magic[3] != kMagic[3]) {
        setError(errorMessage, QStringLiteral("Not a spectrogram file (bad magic)."));
        return false;
    }

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
    ds.setVersion(QDataStream::Qt_5_15);

    quint16 version = 0;
    quint16 flags = 0;
    ds >> version >> flags;
    if (ds.status() != QDataStream::Ok) {
        setError(errorMessage, QStringLiteral("Failed to read version/flags."));
        return false;
    }
    if (version != kVersion) {
        setError(errorMessage,
                 QStringLiteral("Unsupported spectrogram version %1 (need %2).")
                     .arg(version)
                     .arg(kVersion));
        return false;
    }

    quint32 nChannels = 0;
    quint32 nRows = 0;
    ds >> nChannels >> nRows;
    if (ds.status() != QDataStream::Ok || nChannels == 0 || nChannels > 8192
        || nRows > 200000) {
        setError(errorMessage, QStringLiteral("Invalid dimensions."));
        return false;
    }

    float a0 = 0;
    float a1 = 0;
    float a2 = 0;
    quint32 integrate = 1;
    quint32 historyMinutes = 120;
    ds >> a0 >> a1 >> a2 >> integrate >> historyMinutes;
    if (ds.status() != QDataStream::Ok) {
        setError(errorMessage, QStringLiteral("Failed to read header fields."));
        return false;
    }

    quint32 serialLen = 0;
    ds >> serialLen;
    if (ds.status() != QDataStream::Ok || serialLen > 4096) {
        setError(errorMessage, QStringLiteral("Invalid serial length."));
        return false;
    }
    QByteArray serialUtf8;
    if (serialLen > 0) {
        serialUtf8 = f.read(serialLen);
        if (quint32(serialUtf8.size()) != serialLen) {
            setError(errorMessage, QStringLiteral("Truncated serial."));
            return false;
        }
    }

    for (int i = 0; i < 4; ++i) {
        quint32 reserved = 0;
        ds >> reserved;
    }
    if (ds.status() != QDataStream::Ok) {
        setError(errorMessage, QStringLiteral("Failed to read reserved header."));
        return false;
    }

    const bool hasDeltas = (flags & kFlagHasDeltas) != 0;

    doc->nChannels = nChannels;
    doc->a0 = a0;
    doc->a1 = a1;
    doc->a2 = a2;
    doc->integrate = integrate == 0 ? 1 : integrate;
    doc->historyMinutes = historyMinutes == 0 ? 120 : historyMinutes;
    doc->serial = QString::fromUtf8(serialUtf8);
    doc->rows.reserve(int(nRows));

    for (quint32 r = 0; r < nRows; ++r) {
        qint64 wallMs = 0;
        quint32 liveTimeSec = 0;
        quint32 intervalSec = 0;
        ds >> wallMs >> liveTimeSec >> intervalSec;
        if (ds.status() != QDataStream::Ok) {
            setError(errorMessage,
                     QStringLiteral("Truncated file at row %1 header.").arg(r));
            return false;
        }

        Row row;
        row.liveTimeSec = liveTimeSec;
        row.intervalSec = intervalSec;
        row.wallTime = wallMs > 0
                           ? QDateTime::fromMSecsSinceEpoch(wallMs, Qt::LocalTime)
                           : QDateTime();
        row.rates.resize(int(nChannels));
        for (quint32 ch = 0; ch < nChannels; ++ch) {
            float v = 0;
            ds >> v;
            row.rates[int(ch)] = v;
        }
        if (hasDeltas) {
            row.deltas.resize(int(nChannels));
            for (quint32 ch = 0; ch < nChannels; ++ch) {
                quint32 dN = 0;
                ds >> dN;
                row.deltas[int(ch)] = dN;
            }
        }
        if (ds.status() != QDataStream::Ok) {
            setError(errorMessage, QStringLiteral("Truncated file at row %1 data.").arg(r));
            return false;
        }
        doc->rows.append(std::move(row));
    }

    return true;
}

} // namespace SpectrogramFile
