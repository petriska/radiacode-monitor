#include "acquisition/acquisitioncontroller.h"

AcquisitionController::AcquisitionController(QObject *parent)
    : QObject(parent)
{
}

quint64 AcquisitionController::totalCounts(const QtRadiacode::RcSpectrum &sp)
{
    quint64 total = 0;
    for (quint32 c : sp.counts) {
        total += c;
    }
    return total;
}

void AcquisitionController::setState(State s)
{
    if (m_state == s) {
        return;
    }
    m_state = s;
    emit stateChanged(s);
}

void AcquisitionController::start(Mode mode, quint64 target, bool resetSpectrum)
{
    if (m_state != State::Idle) {
        return;
    }
    if (target == 0) {
        emit logMessage(tr("Acquisition: target must be > 0"));
        return;
    }

    m_mode = mode;
    m_target = target;
    m_resetOnStart = resetSpectrum;

    const QString modeText = (mode == Mode::TimeSeconds)
        ? tr("%1 s (device live time)").arg(target)
        : tr("%1 total counts").arg(target);

    if (resetSpectrum) {
        setState(State::WaitingReset);
        emit logMessage(tr("Acquisition starting (reset spectrum) — target %1").arg(modeText));
        emit requestSpectrumReset();
    } else {
        setState(State::Running);
        emit logMessage(tr("Acquisition started — target %1").arg(modeText));
        emit progress(0.0, tr("0% — waiting for spectrum…"));
    }
}

void AcquisitionController::notifySpectrumResetFinished()
{
    if (m_state != State::WaitingReset) {
        return;
    }
    setState(State::Running);
    const QString modeText = (m_mode == Mode::TimeSeconds)
        ? tr("%1 s").arg(m_target)
        : tr("%1 counts").arg(m_target);
    emit logMessage(tr("Acquisition running — target %1").arg(modeText));
    emit progress(0.0, tr("0% — waiting for spectrum…"));
}

void AcquisitionController::abort(const QString &reason)
{
    if (m_state == State::Idle) {
        return;
    }
    setState(State::Idle);
    const QString r = reason.isEmpty() ? tr("stopped") : reason;
    emit logMessage(tr("Acquisition aborted: %1").arg(r));
    emit aborted(r);
    emit progress(0.0, tr("—"));
}

void AcquisitionController::onSpectrum(const QtRadiacode::RcSpectrum &sp)
{
    if (m_state != State::Running) {
        return;
    }

    const quint64 counts = totalCounts(sp);
    const quint32 liveSec = sp.durationSec;
    updateProgress(liveSec, counts);

    bool done = false;
    if (m_mode == Mode::TimeSeconds) {
        done = static_cast<quint64>(liveSec) >= m_target;
    } else {
        done = counts >= m_target;
    }

    if (done) {
        finishCompleted(liveSec, counts);
    }
}

void AcquisitionController::updateProgress(quint32 liveSec, quint64 counts)
{
    double frac = 0.0;
    QString text;
    if (m_mode == Mode::TimeSeconds) {
        frac = m_target > 0 ? qMin(1.0, double(liveSec) / double(m_target)) : 0.0;
        text = tr("%1 / %2 s (%3%)")
                   .arg(liveSec)
                   .arg(m_target)
                   .arg(int(frac * 100.0 + 0.5));
    } else {
        frac = m_target > 0 ? qMin(1.0, double(counts) / double(m_target)) : 0.0;
        text = tr("%1 / %2 counts (%3%)")
                   .arg(counts)
                   .arg(m_target)
                   .arg(int(frac * 100.0 + 0.5));
    }
    emit progress(frac, text);
}

void AcquisitionController::finishCompleted(quint32 liveSec, quint64 counts)
{
    setState(State::Idle);
    const QString summary = (m_mode == Mode::TimeSeconds)
        ? tr("complete: live time %1 s (target %2 s), %3 counts")
              .arg(liveSec)
              .arg(m_target)
              .arg(counts)
        : tr("complete: %1 counts (target %2), live time %3 s")
              .arg(counts)
              .arg(m_target)
              .arg(liveSec);
    emit logMessage(tr("Acquisition %1").arg(summary));
    emit completed(summary);
    // Leave progress at 100% text for the final snapshot.
    updateProgress(liveSec, counts);
}
