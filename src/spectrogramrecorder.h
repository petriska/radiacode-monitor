#pragma once

#include "spectrogramfile.h"

#include <QObject>
#include <QString>

// Continuous spectrogram recording: append rows to a daily .rcsg under
//   <baseDir>/<serial-or-unknown>/YYYY-MM-DD.rcsg
// Rolls at local midnight; closed day files are gzip-compressed to .rcsg.gz (C2).
// Keeps the last keepDays of session files (raw + gzip).
class SpectrogramRecorder : public QObject {
    Q_OBJECT
public:
    explicit SpectrogramRecorder(QObject *parent = nullptr);
    ~SpectrogramRecorder() override;

    bool isRecording() const { return m_recording; }
    QString baseDirectory() const { return m_baseDir; }
    QString currentPath() const { return m_writer.path(); }
    quint32 rowsWritten() const { return m_writer.nRows(); }

    void setBaseDirectory(const QString &dir);

    /// How many calendar days of files to retain (1…3650). Default 30.
    void setKeepDays(int days);
    int keepDays() const { return m_keepDays; }

    /// Compress closed day files to .rcsg.gz (default true).
    void setCompressOnRoll(bool on);
    bool compressOnRoll() const { return m_compressOnRoll; }

    /// Start recording with session metadata (call when enabled + connected).
    bool start(const SpectrogramFile::Header &header, QString *errorMessage = nullptr);
    /// Flush and close the current file (today stays uncompressed for resume).
    void stop();

    /// Append one history row (no-op if not recording).
    bool appendRow(const SpectrogramFile::Row &row, QString *errorMessage = nullptr);

    /// Update calibration fields written only in new files (open session).
    void setSessionMeta(const SpectrogramFile::Header &header);

    /// Delete .rcsg / .rcsg.gz older than keepDays in the session folder.
    int pruneOldFiles();

signals:
    void recordingChanged(bool recording);
    void pathChanged(const QString &path);
    void errorOccurred(const QString &message);

private:
    QString sessionSubdir() const;
    QString pathForDate(const QDate &date) const;
    bool ensureOpenFor(const QDate &date, QString *errorMessage);
    void finalizeClosedFile(const QString &rcsgPath);
    void emitError(const QString &msg);

    SpectrogramFile::AppendWriter m_writer;
    SpectrogramFile::Header m_header;
    QString m_baseDir;
    bool m_recording = false;
    QDate m_openDate;
    int m_keepDays = 30;
    bool m_compressOnRoll = true;
};
