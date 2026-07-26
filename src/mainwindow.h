#pragma once

#include "device/radiacodedevice.h"
#include "protocol/types.h"

#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>

class SpectrumWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void refreshDeviceList();
    void onConnectClicked();
    void onDisconnectClicked();
    void onResetSpectrum();
    void onSaveSpectrum();
    void pollData();

    void onConnected();
    void onDisconnected();
    void onError(const QString &message);
    void onStateChanged(QtRadiacode::RadiaCodeDevice::State state);
    void onDataBuf(const QList<QtRadiacode::RcDataItem> &items);
    void onSpectrum(const QtRadiacode::RcSpectrum &sp);

private:
    void setConnectedUi(bool connected);
    void appendLog(const QString &line);
    static QString formatDuration(quint32 sec);

    QtRadiacode::RadiaCodeDevice *m_device = nullptr;
    QTimer *m_pollTimer = nullptr;
    int m_pollTick = 0;

    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_disconnectBtn = nullptr;
    QPushButton *m_resetSpectrumBtn = nullptr;
    QPushButton *m_saveSpectrumBtn = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_serialLabel = nullptr;
    QLabel *m_fwLabel = nullptr;
    QLabel *m_doseLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_tempLabel = nullptr;
    QLabel *m_spectrumLiveLabel = nullptr;
    QLabel *m_logLabel = nullptr;

    SpectrumWidget *m_spectrum = nullptr;
    QtRadiacode::RcSpectrum m_lastSpectrum;
    bool m_hasSpectrum = false;
};
