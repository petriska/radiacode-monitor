#include "spectrogramrecorder.h"
#include "spectrogramcompress.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

SpectrogramRecorder::SpectrogramRecorder(QObject *parent)
    : QObject(parent)
{
}

SpectrogramRecorder::~SpectrogramRecorder()
{
    stop();
}

void SpectrogramRecorder::setBaseDirectory(const QString &dir)
{
    m_baseDir = dir;
}

void SpectrogramRecorder::setKeepDays(int days)
{
    m_keepDays = qBound(1, days, 3650);
}

void SpectrogramRecorder::setCompressOnRoll(bool on)
{
    m_compressOnRoll = on;
}

void SpectrogramRecorder::setSessionMeta(const SpectrogramFile::Header &header)
{
    m_header = header;
    if (m_header.flags == 0) {
        m_header.flags = SpectrogramFile::kFlagHasDeltas;
    }
}

QString SpectrogramRecorder::sessionSubdir() const
{
    QString serial = m_header.serial.trimmed();
    if (serial.isEmpty()) {
        serial = QStringLiteral("unknown");
    }
    serial.replace(QLatin1Char('/'), QLatin1Char('_'));
    serial.replace(QLatin1Char('\\'), QLatin1Char('_'));
    serial.replace(QLatin1Char(':'), QLatin1Char('_'));
    return serial;
}

QString SpectrogramRecorder::pathForDate(const QDate &date) const
{
    const QString dir = QDir(m_baseDir).filePath(sessionSubdir());
    return QDir(dir).filePath(date.toString(QStringLiteral("yyyy-MM-dd"))
                              + QStringLiteral(".rcsg"));
}

void SpectrogramRecorder::emitError(const QString &msg)
{
    emit errorOccurred(msg);
}

void SpectrogramRecorder::finalizeClosedFile(const QString &rcsgPath)
{
    if (rcsgPath.isEmpty() || !QFileInfo::exists(rcsgPath)) {
        return;
    }
    if (m_compressOnRoll) {
        QString err;
        if (!SpectrogramCompress::compressRcsgInPlace(rcsgPath, &err)) {
            emitError(tr("Compress failed for %1: %2")
                          .arg(QFileInfo(rcsgPath).fileName(), err));
            // Keep the uncompressed .rcsg
        }
    }
    pruneOldFiles();
}

int SpectrogramRecorder::pruneOldFiles()
{
    if (m_baseDir.isEmpty() || m_keepDays <= 0) {
        return 0;
    }
    const QString dirPath = QDir(m_baseDir).filePath(sessionSubdir());
    QDir dir(dirPath);
    if (!dir.exists()) {
        return 0;
    }

    const QDate cutoff = QDate::currentDate().addDays(-m_keepDays);
    // yyyy-MM-dd.rcsg or yyyy-MM-dd.rcsg.gz or yyyy-MM-dd-1024ch.rcsg(.gz)
    static const QRegularExpression re(
        QStringLiteral(R"(^(\d{4}-\d{2}-\d{2})(?:-\d+ch)?\.rcsg(?:\.gz)?$)"));

    int removed = 0;
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : files) {
        const QRegularExpressionMatch m = re.match(fi.fileName());
        if (!m.hasMatch()) {
            continue;
        }
        const QDate d = QDate::fromString(m.captured(1), Qt::ISODate);
        if (!d.isValid() || d >= cutoff) {
            continue;
        }
        // Never delete today's open raw file if we somehow match (shouldn't if open).
        if (m_writer.isOpen() && fi.absoluteFilePath() == m_writer.path()) {
            continue;
        }
        if (QFile::remove(fi.absoluteFilePath())) {
            ++removed;
        }
    }
    return removed;
}

bool SpectrogramRecorder::ensureOpenFor(const QDate &date, QString *errorMessage)
{
    if (m_writer.isOpen() && m_openDate == date
        && m_writer.nChannels() == m_header.nChannels) {
        return true;
    }

    // Roll file on date or channel change.
    QString closedPath;
    if (m_writer.isOpen()) {
        closedPath = m_writer.path();
        QString err;
        if (!m_writer.close(&err) && errorMessage) {
            *errorMessage = err;
        }
        m_openDate = QDate();
        // Compress only when leaving a finished day (not today's mid-session stop).
        if (!closedPath.isEmpty() && m_openDate != date) {
            // m_openDate already cleared — use date comparison via closed file name
        }
        if (!closedPath.isEmpty()) {
            // If we rolled to a different calendar day, compress the closed file.
            // (Channel-only roll on same day: still compress? Prefer leave raw if same day.)
            const QFileInfo cfi(closedPath);
            const QRegularExpression re(QStringLiteral(R"(^(\d{4}-\d{2}-\d{2})"));
            const QRegularExpressionMatch m = re.match(cfi.fileName());
            QDate closedDay;
            if (m.hasMatch()) {
                closedDay = QDate::fromString(m.captured(1), Qt::ISODate);
            }
            if (closedDay.isValid() && closedDay != date) {
                finalizeClosedFile(closedPath);
            }
        }
    }

    if (m_header.nChannels == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot record: channel count is zero.");
        }
        return false;
    }

    const QString path = pathForDate(date);
    QDir().mkpath(QFileInfo(path).absolutePath());

    // If only a .gz exists for this day (user compressed manually), decompress for append.
    const QString gzPath = path + QStringLiteral(".gz");
    if (!QFileInfo::exists(path) && QFileInfo::exists(gzPath)) {
        QString err;
        if (!SpectrogramCompress::gunzipFile(gzPath, path, &err)) {
            if (errorMessage) {
                *errorMessage = err;
            }
            emitError(err);
            return false;
        }
        QFile::remove(gzPath);
    }

    QString err;
    if (QFileInfo::exists(path)) {
        if (!m_writer.openForAppend(path, &err)) {
            if (errorMessage) {
                *errorMessage = err;
            }
            emitError(err);
            return false;
        }
        if (m_writer.nChannels() != m_header.nChannels) {
            m_writer.close();
            const QString alt = QDir(QFileInfo(path).absolutePath())
                                    .filePath(date.toString(QStringLiteral("yyyy-MM-dd"))
                                              + QStringLiteral("-%1ch.rcsg")
                                                    .arg(m_header.nChannels));
            if (QFileInfo::exists(alt)) {
                if (!m_writer.openForAppend(alt, &err)) {
                    if (!m_writer.create(alt, m_header, &err)) {
                        if (errorMessage) {
                            *errorMessage = err;
                        }
                        emitError(err);
                        return false;
                    }
                } else if (m_writer.nChannels() != m_header.nChannels) {
                    m_writer.close();
                    if (!m_writer.create(alt, m_header, &err)) {
                        if (errorMessage) {
                            *errorMessage = err;
                        }
                        emitError(err);
                        return false;
                    }
                }
            } else if (!m_writer.create(alt, m_header, &err)) {
                if (errorMessage) {
                    *errorMessage = err;
                }
                emitError(err);
                return false;
            }
        }
    } else {
        if (!m_writer.create(path, m_header, &err)) {
            if (errorMessage) {
                *errorMessage = err;
            }
            emitError(err);
            return false;
        }
    }

    m_openDate = date;
    emit pathChanged(m_writer.path());
    pruneOldFiles();
    return true;
}

bool SpectrogramRecorder::start(const SpectrogramFile::Header &header, QString *errorMessage)
{
    setSessionMeta(header);
    if (m_baseDir.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Recording directory is not set.");
        }
        return false;
    }
    if (m_header.nChannels == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No spectrum channels yet — connect and wait for data.");
        }
        return false;
    }

    QString err;
    if (!ensureOpenFor(QDate::currentDate(), &err)) {
        if (errorMessage) {
            *errorMessage = err;
        }
        return false;
    }

    if (!m_recording) {
        m_recording = true;
        emit recordingChanged(true);
    }
    pruneOldFiles();
    return true;
}

void SpectrogramRecorder::stop()
{
    // Leave today's .rcsg uncompressed so a later start can append.
    if (m_writer.isOpen()) {
        m_writer.close(nullptr);
        m_openDate = QDate();
        emit pathChanged(QString());
    }
    if (m_recording) {
        m_recording = false;
        emit recordingChanged(false);
    }
}

bool SpectrogramRecorder::appendRow(const SpectrogramFile::Row &row, QString *errorMessage)
{
    if (!m_recording) {
        return true;
    }

    const QDate day = row.wallTime.isValid() ? row.wallTime.date() : QDate::currentDate();
    QString err;
    if (!ensureOpenFor(day, &err)) {
        if (errorMessage) {
            *errorMessage = err;
        }
        return false;
    }

    if (!m_writer.appendRow(row, &err)) {
        if (errorMessage) {
            *errorMessage = err;
        }
        emitError(err);
        return false;
    }

    if (m_writer.nRows() % 30u == 0u) {
        m_writer.flush(nullptr);
    }
    return true;
}
