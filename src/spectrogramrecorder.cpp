#include "spectrogramrecorder.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>

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
    // Safe folder name
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

bool SpectrogramRecorder::ensureOpenFor(const QDate &date, QString *errorMessage)
{
    if (m_writer.isOpen() && m_openDate == date
        && m_writer.nChannels() == m_header.nChannels) {
        return true;
    }

    // Roll file on date or channel change.
    if (m_writer.isOpen()) {
        QString err;
        if (!m_writer.close(&err) && errorMessage) {
            *errorMessage = err;
        }
        m_openDate = QDate();
    }

    if (m_header.nChannels == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot record: channel count is zero.");
        }
        return false;
    }

    const QString path = pathForDate(date);
    QDir().mkpath(QFileInfo(path).absolutePath());

    QString err;
    if (QFileInfo::exists(path)) {
        if (!m_writer.openForAppend(path, &err)) {
            if (errorMessage) {
                *errorMessage = err;
            }
            emitError(err);
            return false;
        }
        // Channel mismatch → new file with suffix (rare: different device same day).
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
    return true;
}

void SpectrogramRecorder::stop()
{
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

    // Periodic OS flush every 30 rows.
    if (m_writer.nRows() % 30u == 0u) {
        m_writer.flush(nullptr);
    }
    return true;
}
