#pragma once

#include "protocol/types.h"

#include <QObject>
#include <QString>

// Timed or count-based spectrum acquisition.
// Stop condition is evaluated on each spectrum snapshot (may overshoot by one frame).
class AcquisitionController : public QObject {
    Q_OBJECT
public:
    enum class Mode { TimeSeconds, TotalCounts };
    Q_ENUM(Mode)

    enum class State { Idle, WaitingReset, Running };
    Q_ENUM(State)

    explicit AcquisitionController(QObject *parent = nullptr);

    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Idle; }
    Mode mode() const { return m_mode; }
    quint64 target() const { return m_target; }

    /// Begin a run. If resetSpectrum is true, emits requestSpectrumReset and stays in
    /// WaitingReset until notifySpectrumResetFinished(); otherwise enters Running immediately.
    void start(Mode mode, quint64 target, bool resetSpectrum);
    /// User cancel or disconnect — not a successful completion.
    void abort(const QString &reason = {});
    /// Call after device spectrumReset operationFinished.
    void notifySpectrumResetFinished();
    /// Feed each live spectrum while active.
    void onSpectrum(const QtRadiacode::RcSpectrum &sp);

    static quint64 totalCounts(const QtRadiacode::RcSpectrum &sp);

signals:
    void stateChanged(AcquisitionController::State state);
    void requestSpectrumReset();
    /// fraction in [0, 1], human-readable progress line.
    void progress(double fraction, const QString &text);
    void completed(const QString &summary);
    void aborted(const QString &reason);
    void logMessage(const QString &line);

private:
    void setState(State s);
    void updateProgress(quint32 liveSec, quint64 counts);
    void finishCompleted(quint32 liveSec, quint64 counts);

    State m_state = State::Idle;
    Mode m_mode = Mode::TimeSeconds;
    quint64 m_target = 60;
    bool m_resetOnStart = true;
};
