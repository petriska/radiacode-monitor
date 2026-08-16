#pragma once

#include "roitimeseries/roimath.h"
#include "roitimeseries/roirecorder.h"

#include "protocol/types.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

class TimeSeriesWidget;
namespace QtRadiacode {
class RadiaCodeDevice;
}

// Universal ROI time-series panel (presets + custom energy windows).
class RoiTimeSeriesPanel : public QWidget {
    Q_OBJECT
public:
    explicit RoiTimeSeriesPanel(QtRadiacode::RadiaCodeDevice *device, QWidget *parent = nullptr);
    ~RoiTimeSeriesPanel() override;

    bool isRecording() const { return m_recording; }
    void onSpectrum(const QtRadiacode::RcSpectrum &sp);
    int dwellSeconds() const;

    /// Preset / table / record controls — placed in the setup ROI tab (chart stays in main tabs).
    QWidget *controlsWidget() const { return m_controlsBox; }

    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    void setConnected(bool connected);
    /// Same as the "Export CSV…" button (ROI time series samples).
    void exportCsv();

signals:
    void recordingChanged(bool recording);
    void requestSpectrumNow();
    void requestSpectrumReset();
    void logMessage(const QString &line);
    /// Current ROI list (for spectrum tinting); includes disabled rows.
    void roisChanged(const QVector<RoiWindow> &rois);

private slots:
    void onStart();
    void onStop();
    void onClear();
    void onSetT0();
    void onPresetChanged(int index);
    void onAddRoi();
    void onRemoveRoi();
    void onTableChanged();

private:
    void updateStatus();
    void loadRoisToTable(const QVector<RoiWindow> &rois);
    QVector<RoiWindow> roisFromTable() const;
    void setEditingEnabled(bool on);
    void emitRoisChanged();
    void loadSettings();
    void saveSettings() const;

    /// Clear highlighted row selection (e.g. click outside the table).
    void clearRoiTableSelection();

    QtRadiacode::RadiaCodeDevice *m_device = nullptr;
    RoiTimeSeriesRecorder *m_recorder = nullptr;
    TimeSeriesWidget *m_chart = nullptr;
    QWidget *m_controlsBox = nullptr;

    QComboBox *m_presetCombo = nullptr;
    QTableWidget *m_roiTable = nullptr;
    QPushButton *m_addRoiBtn = nullptr;
    QPushButton *m_removeRoiBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_t0Btn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QCheckBox *m_resetOnStart = nullptr;
    QComboBox *m_dwellCombo = nullptr;
    QLabel *m_statusLabel = nullptr;

    bool m_recording = false;
    bool m_connected = false;
    bool m_blockTableSignal = false;
};
