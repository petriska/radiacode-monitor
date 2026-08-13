#pragma once

#include "spectrogramfile.h"

#include <QObject>
#include <QString>

// Continuous spectrogram recording: append rows to a daily .rcsg under
//   <baseDir>/<serial-or-unknown>/YYYY-MM-DD.rcsg
// Opens/creates the file as needed; rolls to a new file at local midnight
// or when channel count changes. C1 — no compression yet (C2).
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

    /// Start recording with session metadata (call when enabled + connected).
    bool start(const SpectrogramFile::Header &header, QString *errorMessage = nullptr);
    /// Flush and close the current file.
    void stop();

    /// Append one history row (no-op if not recording).
    bool appendRow(const SpectrogramFile::Row &row, QString *errorMessage = nullptr);

    /// Update calibration fields written only in new files (open session).
    void setSessionMeta(const SpectrogramFile::Header &header);

signals:
    void recordingChanged(bool recording);
    void pathChanged(const QString &path);
    void errorOccurred(const QString &message);

private:
    QString sessionSubdir() const;
    QString pathForDate(const QDate &date) const;
    bool ensureOpenFor(const QDate &date, QString *errorMessage);
    void emitError(const QString &msg);

    SpectrogramFile::AppendWriter m_writer;
    SpectrogramFile::Header m_header;
    QString m_baseDir;
    bool m_recording = false;
    QDate m_openDate;
};
