#pragma once

#include "device/radiacodedevice.h"
#include "discovery/blediscovery.h"
#include "protocol/types.h"

#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>

class SpectrumWidget;
class RoiTimeSeriesPanel;

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

    void onBleDeviceFound(const QtRadiacode::RcBleDeviceInfo &info);
    void onBleScanFinished();

private:
    // Combo roles: UserRole = "usb"|"ble", UserRole+1 = serial or BLE address,
    // UserRole+2 = optional RSSI dBm (int), UserRole+3 = hasRssi (bool).
    static constexpr int RoleTransport = Qt::UserRole;
    static constexpr int RoleId = Qt::UserRole + 1;
    static constexpr int RoleRssi = Qt::UserRole + 2;
    static constexpr int RoleHasRssi = Qt::UserRole + 3;

    void setConnectedUi(bool connected);
    void appendLog(const QString &line);
    static QString formatDuration(quint32 sec);
    bool isBleConnection() const;
    void requestSpectrumGated();
    void configurePollForConnection();
    bool spectrumDwellDueMs(int dwellSeconds) const;
    void startSlowStatusTimer();
    void stopSlowStatusTimer();
    void onSlowStatusTick();
    void updateSignalLabel(int rssiDbm, bool fromScan = false);

    int addUsbDevicesToCombo();
    void stopBleScan();
    void startBleScan();
    void removeEmptyPlaceholder();
    void ensureEmptyPlaceholder();
    void updateRefreshButton();
    int findDeviceRow(const QString &transport, const QString &id) const;

    static constexpr int kPollIntervalUsbMs = 1000;
    // BLE: 1 s tick for dwell timing; at most one command per tick (see pollData).
    static constexpr int kPollIntervalBleMs = 1000;

    static constexpr int kSlowStatusIntervalMs = 60 * 1000;

    QtRadiacode::RadiaCodeDevice *m_device = nullptr;
    QtRadiacode::RcBleScanner *m_bleScanner = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_slowStatusTimer = nullptr;
    int m_pollTick = 0;
    bool m_bleScanActive = false;
    int m_bleFoundThisScan = 0;
    // Avoid stacking spectrum reads (large BLE payloads) until the previous finishes.
    bool m_spectrumInflight = false;
    // Wall-clock since last completed spectrum sample (ROI dwell / BLE live).
    QElapsedTimer m_sinceSpectrumSample;
    bool m_haveSpectrumSample = false;

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
    QLabel *m_batteryLabel = nullptr;
    QLabel *m_signalLabel = nullptr;
    QLabel *m_spectrumLiveLabel = nullptr;
    QLabel *m_spectrumTotalLabel = nullptr;
    QLabel *m_logLabel = nullptr;

    SpectrumWidget *m_spectrum = nullptr;
    RoiTimeSeriesPanel *m_roiPanel = nullptr;
    QtRadiacode::RcSpectrum m_lastSpectrum;
    bool m_hasSpectrum = false;
};
