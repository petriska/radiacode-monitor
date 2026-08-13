#include "spectrogramfile.h"

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

void configureStream(QDataStream &ds)
{
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
    ds.setVersion(QDataStream::Qt_5_15);
}

} // namespace

QString fileFilter()
{
    return QStringLiteral(
        "Spectrogram (*.rcsg *.rcsg.gz);;Raw RCSG (*.rcsg);;Gzip RCSG (*.rcsg.gz);;All files (*.*)");
}

QString defaultExtension()
{
    return QStringLiteral("rcsg");
}

qint64 rowByteSize(quint32 nChannels, bool hasDeltas)
{
    // i64 + u32 + u32 + n*f32 + optional n*u32
    qint64 n = 8 + 4 + 4 + qint64(nChannels) * 4;
    if (hasDeltas) {
        n += qint64(nChannels) * 4;
    }
    return n;
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

    Header h;
    h.nChannels = doc.nChannels;
    h.a0 = doc.a0;
    h.a1 = doc.a1;
    h.a2 = doc.a2;
    h.integrate = doc.integrate;
    h.historyMinutes = doc.historyMinutes;
    h.serial = doc.serial;
    h.flags = kFlagHasDeltas;

    AppendWriter w;
    if (!w.create(path, h, errorMessage)) {
        return false;
    }
    for (const Row &row : doc.rows) {
        if (!w.appendRow(row, errorMessage)) {
            w.close();
            return false;
        }
    }
    return w.close(errorMessage);
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
    configureStream(ds);

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
        || nRows > 500000) {
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
        row.wallTime = wallMs > 0 ? QDateTime::fromMSecsSinceEpoch(wallMs)
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

// --- AppendWriter ----------------------------------------------------------

AppendWriter::~AppendWriter()
{
    if (m_file.isOpen()) {
        close(nullptr);
    }
}

bool AppendWriter::writeHeader(const Header &header, quint32 nRows, QString *errorMessage)
{
    if (m_file.write(kMagic, 4) != 4) {
        setError(errorMessage, QStringLiteral("Failed to write magic."));
        return false;
    }

    QDataStream ds(&m_file);
    configureStream(ds);

    const quint16 flags = header.flags ? header.flags : kFlagHasDeltas;
    if (!writeU16(ds, kVersion) || !writeU16(ds, flags)) {
        setError(errorMessage, QStringLiteral("Failed to write version/flags."));
        return false;
    }
    if (!writeU32(ds, header.nChannels) || !writeU32(ds, nRows)) {
        setError(errorMessage, QStringLiteral("Failed to write dimensions."));
        return false;
    }
    if (!writeF32(ds, header.a0) || !writeF32(ds, header.a1) || !writeF32(ds, header.a2)) {
        setError(errorMessage, QStringLiteral("Failed to write calibration."));
        return false;
    }
    if (!writeU32(ds, header.integrate) || !writeU32(ds, header.historyMinutes)) {
        setError(errorMessage, QStringLiteral("Failed to write integrate/history."));
        return false;
    }

    const QByteArray serialUtf8 = header.serial.toUtf8();
    if (!writeU32(ds, quint32(serialUtf8.size()))) {
        setError(errorMessage, QStringLiteral("Failed to write serial length."));
        return false;
    }
    if (!serialUtf8.isEmpty() && m_file.write(serialUtf8) != serialUtf8.size()) {
        setError(errorMessage, QStringLiteral("Failed to write serial."));
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!writeU32(ds, 0)) {
            setError(errorMessage, QStringLiteral("Failed to write reserved header."));
            return false;
        }
    }
    m_dataStart = m_file.pos();
    m_hasDeltas = (flags & kFlagHasDeltas) != 0;
    return ds.status() == QDataStream::Ok;
}

bool AppendWriter::create(const QString &path, const Header &header, QString *errorMessage)
{
    close(nullptr);
    if (header.nChannels == 0 || header.nChannels > 8192) {
        setError(errorMessage, QStringLiteral("Invalid channel count."));
        return false;
    }

    m_path = path;
    m_header = header;
    m_header.flags = header.flags ? header.flags : kFlagHasDeltas;
    m_nChannels = header.nChannels;
    m_nRows = 0;

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(errorMessage, m_file.errorString());
        return false;
    }
    if (!writeHeader(m_header, 0, errorMessage)) {
        m_file.close();
        return false;
    }
    return true;
}

bool AppendWriter::openForAppend(const QString &path, QString *errorMessage)
{
    close(nullptr);

    m_path = path;
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadWrite)) {
        setError(errorMessage, m_file.errorString());
        return false;
    }

    char magic[4];
    if (m_file.read(magic, 4) != 4
        || magic[0] != kMagic[0] || magic[1] != kMagic[1]
        || magic[2] != kMagic[2] || magic[3] != kMagic[3]) {
        setError(errorMessage, QStringLiteral("Not a spectrogram file (bad magic)."));
        m_file.close();
        return false;
    }

    QDataStream ds(&m_file);
    configureStream(ds);

    quint16 version = 0;
    quint16 flags = 0;
    ds >> version >> flags;
    if (version != kVersion) {
        setError(errorMessage, QStringLiteral("Unsupported version for append."));
        m_file.close();
        return false;
    }

    quint32 nChannels = 0;
    quint32 nRows = 0;
    ds >> nChannels >> nRows;
    float a0 = 0;
    float a1 = 0;
    float a2 = 0;
    quint32 integrate = 1;
    quint32 historyMinutes = 120;
    ds >> a0 >> a1 >> a2 >> integrate >> historyMinutes;
    quint32 serialLen = 0;
    ds >> serialLen;
    if (ds.status() != QDataStream::Ok || nChannels == 0 || serialLen > 4096) {
        setError(errorMessage, QStringLiteral("Corrupt header."));
        m_file.close();
        return false;
    }
    QByteArray serialUtf8;
    if (serialLen > 0) {
        serialUtf8 = m_file.read(serialLen);
        if (quint32(serialUtf8.size()) != serialLen) {
            setError(errorMessage, QStringLiteral("Truncated serial."));
            m_file.close();
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        quint32 reserved = 0;
        ds >> reserved;
    }

    m_hasDeltas = (flags & kFlagHasDeltas) != 0;
    m_dataStart = m_file.pos();
    m_nChannels = nChannels;
    m_nRows = nRows;
    m_header.nChannels = nChannels;
    m_header.a0 = a0;
    m_header.a1 = a1;
    m_header.a2 = a2;
    m_header.integrate = integrate;
    m_header.historyMinutes = historyMinutes;
    m_header.serial = QString::fromUtf8(serialUtf8);
    m_header.flags = flags;

    // Seek to end of complete rows (ignore trailing partial row after crash).
    const qint64 rowBytes = rowByteSize(nChannels, m_hasDeltas);
    const qint64 expectedEnd = m_dataStart + qint64(nRows) * rowBytes;
    const qint64 fileSize = m_file.size();
    if (fileSize < m_dataStart) {
        setError(errorMessage, QStringLiteral("File too small."));
        m_file.close();
        return false;
    }
    // If file longer than header claims, recover extra full rows.
    if (fileSize > expectedEnd && rowBytes > 0) {
        const qint64 extra = (fileSize - m_dataStart) / rowBytes;
        if (extra > qint64(nRows)) {
            m_nRows = quint32(extra);
        }
    }
    if (!m_file.seek(m_dataStart + qint64(m_nRows) * rowBytes)) {
        setError(errorMessage, QStringLiteral("Seek failed."));
        m_file.close();
        return false;
    }
    return true;
}

bool AppendWriter::writeRowPayload(const Row &row, QString *errorMessage)
{
    if (row.rates.size() != int(m_nChannels)) {
        setError(errorMessage, QStringLiteral("Row channel count mismatch."));
        return false;
    }

    QDataStream ds(&m_file);
    configureStream(ds);

    const qint64 wallMs = row.wallTime.isValid() ? row.wallTime.toMSecsSinceEpoch() : qint64(0);
    if (!writeI64(ds, wallMs) || !writeU32(ds, row.liveTimeSec)
        || !writeU32(ds, row.intervalSec)) {
        setError(errorMessage, QStringLiteral("Failed to write row header."));
        return false;
    }
    for (int ch = 0; ch < int(m_nChannels); ++ch) {
        if (!writeF32(ds, row.rates.at(ch))) {
            setError(errorMessage, QStringLiteral("Failed to write rates."));
            return false;
        }
    }
    if (m_hasDeltas) {
        for (int ch = 0; ch < int(m_nChannels); ++ch) {
            const quint32 dN = (ch < row.deltas.size()) ? row.deltas.at(ch) : 0u;
            if (!writeU32(ds, dN)) {
                setError(errorMessage, QStringLiteral("Failed to write deltas."));
                return false;
            }
        }
    }
    return ds.status() == QDataStream::Ok;
}

bool AppendWriter::patchNRows(QString *errorMessage)
{
    const qint64 pos = m_file.pos();
    if (!m_file.seek(kNRowsFieldOffset)) {
        setError(errorMessage, QStringLiteral("Seek nRows failed."));
        return false;
    }
    QDataStream ds(&m_file);
    configureStream(ds);
    if (!writeU32(ds, m_nRows)) {
        setError(errorMessage, QStringLiteral("Patch nRows failed."));
        return false;
    }
    if (!m_file.seek(pos)) {
        setError(errorMessage, QStringLiteral("Seek restore failed."));
        return false;
    }
    return true;
}

bool AppendWriter::appendRow(const Row &row, QString *errorMessage)
{
    if (!m_file.isOpen()) {
        setError(errorMessage, QStringLiteral("Writer not open."));
        return false;
    }
    if (!writeRowPayload(row, errorMessage)) {
        return false;
    }
    ++m_nRows;
    // Patch nRows every row so a crash loses at most the in-flight write.
    if (!patchNRows(errorMessage)) {
        return false;
    }
    return true;
}

bool AppendWriter::flush(QString *errorMessage)
{
    if (!m_file.isOpen()) {
        return true;
    }
    if (!patchNRows(errorMessage)) {
        return false;
    }
    if (!m_file.flush()) {
        setError(errorMessage, m_file.errorString());
        return false;
    }
    return true;
}

bool AppendWriter::close(QString *errorMessage)
{
    if (!m_file.isOpen()) {
        return true;
    }
    const bool ok = flush(errorMessage);
    m_file.close();
    m_path.clear();
    m_nChannels = 0;
    m_nRows = 0;
    m_dataStart = 0;
    return ok;
}

} // namespace SpectrogramFile
