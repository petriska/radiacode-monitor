#include "mainwindow.h"
#include "roitimeseries/roimath.h"
#include "roitimeseries/roitimeseriespanel.h"
#include "spectrumexport.h"
#include "spectrumwidget.h"

#include "discovery/usbdiscovery.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Radiacode Monitor"));
    resize(1100, 700);

    m_device = new QtRadiacode::RadiaCodeDevice(this);

    // Always release USB on quit so the device is not left claimed after kill/close.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        m_pollTimer->stop();
        if (m_device
            && m_device->state() != QtRadiacode::RadiaCodeDevice::State::Disconnected) {
            m_device->disconnectFromDevice();
        }
    });
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::pollData);

    m_slowStatusTimer = new QTimer(this);
    m_slowStatusTimer->setInterval(kSlowStatusIntervalMs);
    connect(m_slowStatusTimer, &QTimer::timeout, this, &MainWindow::onSlowStatusTick);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);

    // Connection bar
    auto *connBox = new QGroupBox(tr("Device"), this);
    auto *connLay = new QHBoxLayout(connBox);
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setMinimumWidth(320);
    m_refreshBtn = new QPushButton(tr("Refresh"), this);
    m_connectBtn = new QPushButton(tr("Connect"), this);
    m_disconnectBtn = new QPushButton(tr("Disconnect"), this);
    m_resetSpectrumBtn = new QPushButton(tr("Reset spectrum"), this);
    m_resetSpectrumBtn->setToolTip(
        tr("Clear the spectrum accumulation on the device.\n"
           "The plot auto-refreshes about every 2 seconds while connected."));
    m_saveSpectrumBtn = new QPushButton(tr("Save spectrum…"), this);
    m_saveSpectrumBtn->setToolTip(
        tr("Save the last spectrum as CSV, TKA, ANSI/IEEE N42.42, or NPES-JSON.\n"
           "Includes live time and energy calibration where the format allows."));
    m_refreshBtn->setToolTip(
        tr("Re-scan USB and BLE for Radiacode devices.\n"
           "BLE: device must be free (not held by phone or Home Assistant)."));
    m_connectBtn->setToolTip(tr("Connect via USB or BLE to the selected device"));
    m_disconnectBtn->setToolTip(tr("Close connection and release the device"));
    connLay->addWidget(m_deviceCombo, 1);
    connLay->addWidget(m_refreshBtn);
    connLay->addWidget(m_connectBtn);
    connLay->addWidget(m_disconnectBtn);
    connLay->addWidget(m_resetSpectrumBtn);
    connLay->addWidget(m_saveSpectrumBtn);
    root->addWidget(connBox);

    // Live (left) + ROI controls (right) — visible on Spectrum and ROI tabs.
    auto *topRow = new QHBoxLayout;
    auto *liveBox = new QGroupBox(tr("Live"), this);
    auto *form = new QFormLayout(liveBox);
    m_statusLabel = new QLabel(tr("Disconnected"), this);
    m_serialLabel = new QLabel(QStringLiteral("—"), this);
    m_fwLabel = new QLabel(QStringLiteral("—"), this);
    m_doseLabel = new QLabel(QStringLiteral("—"), this);
    m_countLabel = new QLabel(QStringLiteral("—"), this);
    m_tempLabel = new QLabel(QStringLiteral("—"), this);
    m_spectrumLiveLabel = new QLabel(QStringLiteral("—"), this);
    m_spectrumLiveLabel->setToolTip(
        tr("Live time of the spectrum currently shown (device accumulation clock)."));
    m_batteryLabel = new QLabel(QStringLiteral("—"), this);
    m_batteryLabel->setToolTip(
        tr("Battery %% from Rare DATA_BUF (device status record).\n"
           "Not a separate request — the device inserts Rare records into the "
           "measurement stream periodically; we poll DATA_BUF about once a minute."));
    m_signalLabel = new QLabel(QStringLiteral("—"), this);
    m_signalLabel->setToolTip(
        tr("BLE link strength (RSSI). USB shows n/a.\n"
           "Updated from scan and about once per minute while connected (if the stack supports it)."));
    form->addRow(tr("Status"), m_statusLabel);
    form->addRow(tr("Serial"), m_serialLabel);
    form->addRow(tr("Firmware"), m_fwLabel);
    form->addRow(tr("Dose rate (protocol units)"), m_doseLabel);
    form->addRow(tr("Count rate (CPS)"), m_countLabel);
    form->addRow(tr("Temperature"), m_tempLabel);
    form->addRow(tr("Battery"), m_batteryLabel);
    form->addRow(tr("BLE signal"), m_signalLabel);
    form->addRow(tr("Spectrum live time"), m_spectrumLiveLabel);
    liveBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    topRow->addWidget(liveBox, 2);

    m_roiPanel = new RoiTimeSeriesPanel(m_device, this);
    if (QWidget *roiCtrl = m_roiPanel->controlsWidget()) {
        roiCtrl->setMinimumWidth(360);
        roiCtrl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        // Reparent into the top row (still owned logically by the panel).
        topRow->addWidget(roiCtrl, 3);
    }
    root->addLayout(topRow);

    auto *tabs = new QTabWidget(this);
    m_spectrum = new SpectrumWidget(this);
    tabs->addTab(m_spectrum, tr("Spectrum"));
    // ROI tab: chart only (controls are above, next to Live).
    tabs->addTab(m_roiPanel, tr("ROI time series"));
    root->addWidget(tabs, 1);

    connect(m_spectrum, &SpectrumWidget::cursorInfoChanged, this,
            [this](int channel, double energyKeV, quint32 counts) {
        if (channel < 0) {
            return;
        }
        // Brief status line while hovering the spectrum cursor.
        if (qAbs(m_lastSpectrum.a1) > 1e-12f || qAbs(m_lastSpectrum.a2) > 1e-12f
            || qAbs(m_lastSpectrum.a0) > 1e-12f) {
            statusBar()->showMessage(
                tr("Cursor: E = %1 keV · ch %2 · N = %3")
                    .arg(energyKeV, 0, 'f', 1)
                    .arg(channel)
                    .arg(counts),
                2000);
        } else {
            statusBar()->showMessage(
                tr("Cursor: ch %1 · N = %2").arg(channel).arg(counts), 2000);
        }
    });

    connect(m_roiPanel, &RoiTimeSeriesPanel::logMessage, this, &MainWindow::appendLog);
    connect(m_roiPanel, &RoiTimeSeriesPanel::roisChanged, this,
            [this](const QVector<RoiWindow> &rois) {
        QVector<SpectrumRoiBand> bands;
        bands.reserve(rois.size());
        for (int i = 0; i < rois.size(); ++i) {
            const RoiWindow &r = rois[i];
            SpectrumRoiBand b;
            b.eMinKeV = r.eMinKeV;
            b.eMaxKeV = r.eMaxKeV;
            b.enabled = r.enabled;
            // Match chart palette: index 0 = gross, ROIs start at 1.
            b.color = roiSeriesColor(i + 1);
            bands.append(b);
        }
        m_spectrum->setRoiBands(bands);
    });
    connect(m_roiPanel, &RoiTimeSeriesPanel::requestSpectrumNow, this, [this] {
        if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected) {
            m_device->requestSpectrum();
        }
    });
    // Reset then wait for worker completion before the first spectrum read — avoids
    // USB response desync when reset is interleaved with the 1 s dataBuf/temperature poll.
    connect(m_roiPanel, &RoiTimeSeriesPanel::requestSpectrumReset, this, [this] {
        if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
            return;
        }
        m_device->spectrumReset();
    });
    connect(m_device, &QtRadiacode::RadiaCodeDevice::operationFinished, this,
            [this](const QString &op) {
        if (op != QStringLiteral("spectrumReset")) {
            return;
        }
        if (!m_roiPanel || !m_roiPanel->isRecording()) {
            return;
        }
        if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
            return;
        }
        // Brief settle after WR_VIRT_STRING spectrum clear, then first sample.
        QTimer::singleShot(150, this, [this] {
            if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected
                && m_roiPanel && m_roiPanel->isRecording()) {
                m_device->requestSpectrum();
            }
        });
    });

    // Last events / errors — also mirrored to the status bar (bottom of the window).
    auto *msgBox = new QGroupBox(tr("Messages (log)"), this);
    auto *msgLay = new QVBoxLayout(msgBox);
    m_logLabel = new QLabel(tr("Ready."), this);
    m_logLabel->setWordWrap(true);
    m_logLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_logLabel->setMinimumHeight(48);
    m_logLabel->setStyleSheet(
        QStringLiteral("color: #ddd; font-size: 12px; background: #1a1a1e; padding: 6px;"));
    msgLay->addWidget(m_logLabel);
    root->addWidget(msgBox);

    statusBar()->showMessage(tr("Ready — pick a device and Connect."));

    m_bleScanner = new QtRadiacode::RcBleScanner(this);

    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshDeviceList);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(m_resetSpectrumBtn, &QPushButton::clicked, this, &MainWindow::onResetSpectrum);
    connect(m_saveSpectrumBtn, &QPushButton::clicked, this, &MainWindow::onSaveSpectrum);

    connect(m_device, &QtRadiacode::RadiaCodeDevice::connected, this, &MainWindow::onConnected);
    connect(m_device, &QtRadiacode::RadiaCodeDevice::disconnected, this, &MainWindow::onDisconnected);
    connect(m_device, &QtRadiacode::RadiaCodeDevice::errorOccurred, this, &MainWindow::onError);
    connect(m_device, &QtRadiacode::RadiaCodeDevice::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_device, &QtRadiacode::RadiaCodeDevice::dataBufReady, this, &MainWindow::onDataBuf);
    connect(m_device, &QtRadiacode::RadiaCodeDevice::spectrumReady, this, &MainWindow::onSpectrum);
    connect(m_device, &QtRadiacode::RadiaCodeDevice::temperatureReady, this, [this](float c) {
        m_tempLabel->setText(QStringLiteral("%1 °C").arg(c, 0, 'f', 1));
    });
    connect(m_device, &QtRadiacode::RadiaCodeDevice::bleRssiReady, this, [this](int rssiDbm) {
        updateSignalLabel(rssiDbm, false);
    });

    setConnectedUi(false);
    refreshDeviceList();
}

MainWindow::~MainWindow()
{
    m_pollTimer->stop();
    stopBleScan();
    if (m_device && m_device->state() != QtRadiacode::RadiaCodeDevice::State::Disconnected) {
        m_device->disconnectFromDevice();
    }
}

int MainWindow::findDeviceRow(const QString &transport, const QString &id) const
{
    for (int i = 0; i < m_deviceCombo->count(); ++i) {
        if (m_deviceCombo->itemData(i, RoleTransport).toString() == transport
            && m_deviceCombo->itemData(i, RoleId).toString() == id) {
            return i;
        }
    }
    return -1;
}

void MainWindow::removeEmptyPlaceholder()
{
    for (int i = m_deviceCombo->count() - 1; i >= 0; --i) {
        if (m_deviceCombo->itemData(i, RoleTransport).toString().isEmpty()) {
            m_deviceCombo->removeItem(i);
        }
    }
}

void MainWindow::ensureEmptyPlaceholder()
{
    for (int i = 0; i < m_deviceCombo->count(); ++i) {
        if (!m_deviceCombo->itemData(i, RoleTransport).toString().isEmpty()) {
            return;
        }
    }
    m_deviceCombo->clear();
    m_deviceCombo->addItem(
        tr("(no devices — check USB/udev, BLE on, not held by phone/HA)"),
        QVariant());
    m_deviceCombo->setItemData(0, QString(), RoleTransport);
    m_deviceCombo->setItemData(0, QString(), RoleId);
}

void MainWindow::updateRefreshButton()
{
    using S = QtRadiacode::RadiaCodeDevice::State;
    const bool connected =
        m_device
        && (m_device->state() == S::Connected || m_device->state() == S::Connecting
            || m_device->state() == S::Disconnecting);

    if (m_bleScanActive) {
        m_refreshBtn->setText(tr("Scanning…"));
        m_refreshBtn->setEnabled(false);
    } else {
        m_refreshBtn->setText(tr("Refresh"));
        m_refreshBtn->setEnabled(!connected);
    }
}

int MainWindow::addUsbDevicesToCombo()
{
    const auto devices = QtRadiacode::listUsbDevices();
    for (const auto &d : devices) {
        const QString label = d.serialNumber.isEmpty()
            ? tr("[USB] %1").arg(d.product.isEmpty() ? tr("(unknown)") : d.product)
            : tr("[USB] %1  (%2)")
                  .arg(d.serialNumber, d.product.isEmpty() ? tr("Radiacode") : d.product);
        m_deviceCombo->addItem(label);
        const int row = m_deviceCombo->count() - 1;
        m_deviceCombo->setItemData(row, QStringLiteral("usb"), RoleTransport);
        m_deviceCombo->setItemData(row, d.serialNumber, RoleId);
    }
    return devices.size();
}

void MainWindow::stopBleScan()
{
    if (!m_bleScanner) {
        m_bleScanActive = false;
        return;
    }
    // Avoid finished() re-entering UI while we rebuild the list.
    disconnect(m_bleScanner, nullptr, this, nullptr);
    m_bleScanner->stop();
    m_bleScanActive = false;
}

void MainWindow::startBleScan()
{
    using S = QtRadiacode::RadiaCodeDevice::State;
    if (m_device && m_device->state() != S::Disconnected && m_device->state() != S::Error) {
        return;
    }

    stopBleScan();
    if (!m_bleScanner) {
        m_bleScanner = new QtRadiacode::RcBleScanner(this);
    }

    connect(m_bleScanner, &QtRadiacode::RcBleScanner::deviceFound, this,
            &MainWindow::onBleDeviceFound);
    connect(m_bleScanner, &QtRadiacode::RcBleScanner::finished, this,
            &MainWindow::onBleScanFinished);
    connect(m_bleScanner, &QtRadiacode::RcBleScanner::errorOccurred, this,
            [this](const QString &msg) { appendLog(msg); });

    m_bleScanActive = true;
    m_bleFoundThisScan = 0;
    updateRefreshButton();
    appendLog(tr("BLE scan started (10 s)…"));
    m_bleScanner->start(10000);
}

void MainWindow::onBleDeviceFound(const QtRadiacode::RcBleDeviceInfo &info)
{
    if (info.address.isEmpty()) {
        return;
    }
    if (findDeviceRow(QStringLiteral("ble"), info.address) >= 0) {
        return;
    }

    removeEmptyPlaceholder();
    QString label = tr("[BLE] %1  %2").arg(info.name, info.address);
    if (info.hasRssi) {
        label += tr("  %1 dBm").arg(info.rssiDbm);
    }
    m_deviceCombo->addItem(label);
    const int row = m_deviceCombo->count() - 1;
    m_deviceCombo->setItemData(row, QStringLiteral("ble"), RoleTransport);
    m_deviceCombo->setItemData(row, info.address, RoleId);
    m_deviceCombo->setItemData(row, info.rssiDbm, RoleRssi);
    m_deviceCombo->setItemData(row, info.hasRssi, RoleHasRssi);
    ++m_bleFoundThisScan;
}

void MainWindow::onBleScanFinished()
{
    m_bleScanActive = false;
    ensureEmptyPlaceholder();
    updateRefreshButton();
    appendLog(tr("BLE scan finished: %1 device(s)").arg(m_bleFoundThisScan));
}

void MainWindow::refreshDeviceList()
{
    using S = QtRadiacode::RadiaCodeDevice::State;
    if (m_device
        && (m_device->state() == S::Connected || m_device->state() == S::Connecting
            || m_device->state() == S::Disconnecting)) {
        return;
    }

    const QString prevTransport = m_deviceCombo->currentData(RoleTransport).toString();
    const QString prevId = m_deviceCombo->currentData(RoleId).toString();

    stopBleScan();
    m_deviceCombo->clear();

    const int usbCount = addUsbDevicesToCombo();
    if (usbCount == 0) {
        // Placeholder until BLE results arrive (or scan ends empty).
        ensureEmptyPlaceholder();
    }

    const int idx = findDeviceRow(prevTransport, prevId);
    if (idx >= 0) {
        m_deviceCombo->setCurrentIndex(idx);
    }

    appendLog(tr("USB list: %1 device(s) — scanning BLE…").arg(usbCount));
    startBleScan();
}

void MainWindow::onConnectClicked()
{
    const QString transport = m_deviceCombo->currentData(RoleTransport).toString();
    const QString id = m_deviceCombo->currentData(RoleId).toString();

    if (transport.isEmpty()) {
        appendLog(tr("No device selected — Refresh and pick USB or BLE."));
        return;
    }

    // Stop BLE scan so it does not fight the adapter during connect.
    stopBleScan();
    updateRefreshButton();

    m_connectBtn->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_deviceCombo->setEnabled(false);

    if (transport == QLatin1String("ble")) {
        if (id.isEmpty()) {
            appendLog(tr("BLE device has no address."));
            setConnectedUi(false);
            return;
        }
        appendLog(tr("Connecting BLE %1…").arg(id));
        m_device->connectBle(id);
        return;
    }

    appendLog(tr("Connecting USB %1…").arg(id.isEmpty() ? tr("(first)") : id));
    m_device->connectUsb(id);
}

void MainWindow::onDisconnectClicked()
{
    // Stop polling immediately so no new request* race the close path.
    m_pollTimer->stop();
    m_disconnectBtn->setEnabled(false);
    appendLog(tr("Disconnecting…"));
    m_device->disconnectFromDevice();
}

void MainWindow::onResetSpectrum()
{
    if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
        return;
    }
    appendLog(tr("Resetting spectrum on device…"));
    m_spectrum->clear();
    m_hasSpectrum = false;
    m_lastSpectrum = {};
    m_spectrumLiveLabel->setText(QStringLiteral("—"));
    m_saveSpectrumBtn->setEnabled(false);
    m_spectrumInflight = false;
    m_device->spectrumReset();
    // Next auto-poll will reload; also request once after a short delay.
    QTimer::singleShot(isBleConnection() ? 800 : 300, this, [this] {
        if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected) {
            requestSpectrumGated();
        }
    });
}

QString MainWindow::formatDuration(quint32 sec)
{
    const quint32 h = sec / 3600;
    const quint32 m = (sec % 3600) / 60;
    const quint32 s = sec % 60;
    if (h > 0) {
        return QStringLiteral("%1 h %2 min %3 s (%4 s total)")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'))
            .arg(sec);
    }
    if (m > 0) {
        return QStringLiteral("%1 min %2 s (%3 s total)")
            .arg(m)
            .arg(s, 2, 10, QLatin1Char('0'))
            .arg(sec);
    }
    return QStringLiteral("%1 s").arg(sec);
}

void MainWindow::onSaveSpectrum()
{
    if (!m_hasSpectrum || m_lastSpectrum.counts.isEmpty()) {
        appendLog(tr("No spectrum to save yet."));
        return;
    }

    const QString serial = m_device->serialNumber().isEmpty()
        ? QStringLiteral("unknown")
        : m_device->serialNumber();
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    // Basename without extension — suffix is applied from the selected format.
    const QString baseName = QStringLiteral("%1_spectrum_%2s_%3")
                                 .arg(serial)
                                 .arg(m_lastSpectrum.durationSec)
                                 .arg(stamp);

    QSettings settings;
    const auto lastFormat = SpectrumExport::formatFromSettingsKey(
        settings.value(QStringLiteral("spectrumExport/format"), QStringLiteral("csv"))
            .toString());
    const QString lastDir = settings
                                .value(
                                    QStringLiteral("spectrumExport/dir"),
                                    QStandardPaths::writableLocation(
                                        QStandardPaths::DocumentsLocation))
                                .toString();
    const QString startDir =
        QDir(lastDir).exists()
            ? lastDir
            : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    QFileDialog dlg(this, tr("Save spectrum"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setNameFilters(SpectrumExport::formatFilterString().split(QStringLiteral(";;")));
    dlg.selectNameFilter(SpectrumExport::nameFilterForFormat(lastFormat));
    dlg.setDirectory(startDir);
    dlg.selectFile(baseName); // no extension in the name field
    dlg.setDefaultSuffix(SpectrumExport::defaultExtension(lastFormat));
    dlg.setOption(QFileDialog::DontConfirmOverwrite, false);

    // When the user changes CSV / TKA / N42, update default suffix and filename.
    QObject::connect(
        &dlg,
        &QFileDialog::filterSelected,
        &dlg,
        [&dlg](const QString &filter) {
            const auto fmt = SpectrumExport::formatFromFilter(filter);
            const QString ext = SpectrumExport::defaultExtension(fmt);
            dlg.setDefaultSuffix(ext);

            QStringList sel = dlg.selectedFiles();
            if (sel.isEmpty()) {
                return;
            }
            // Keep directory + basename; force extension for the new format.
            const QFileInfo fi(sel.constFirst());
            const QString stem = SpectrumExport::stripKnownExtension(fi.fileName());
            if (stem.isEmpty()) {
                return;
            }
            dlg.selectFile(fi.dir().filePath(stem)); // still without ext; defaultSuffix adds it
        });

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QStringList files = dlg.selectedFiles();
    if (files.isEmpty()) {
        return;
    }

    const auto format = SpectrumExport::formatFromFilter(dlg.selectedNameFilter());
    // Always finalize extension from the chosen format (not from a stale name).
    QString path = SpectrumExport::withExtension(files.constFirst(), format);

    const QString err = SpectrumExport::writeSpectrumFile(
        path,
        format,
        m_lastSpectrum,
        serial,
        m_device->firmwareVersion());
    if (!err.isEmpty()) {
        appendLog(tr("Save failed: %1").arg(err));
        return;
    }

    // Remember format (and directory) for the next Save spectrum… dialog.
    settings.setValue(
        QStringLiteral("spectrumExport/format"),
        SpectrumExport::formatSettingsKey(format));
    settings.setValue(
        QStringLiteral("spectrumExport/dir"),
        QFileInfo(path).absolutePath());

    QString fmtName = QStringLiteral("CSV");
    if (format == SpectrumExport::Format::Tka) {
        fmtName = QStringLiteral("TKA");
    } else if (format == SpectrumExport::Format::N42) {
        fmtName = QStringLiteral("N42.42");
    } else if (format == SpectrumExport::Format::Npes) {
        fmtName = QStringLiteral("NPES-JSON");
    }

    appendLog(tr("Spectrum saved as %1 (%2 s live, %3 ch) → %4")
                  .arg(fmtName)
                  .arg(m_lastSpectrum.durationSec)
                  .arg(m_lastSpectrum.counts.size())
                  .arg(path));
}

bool MainWindow::isBleConnection() const
{
    return m_device
        && m_device->connectionType() == QtRadiacode::RadiaCodeDevice::ConnectionType::Ble;
}

void MainWindow::requestSpectrumGated()
{
    if (m_spectrumInflight) {
        return;
    }
    m_spectrumInflight = true;
    m_device->requestSpectrum();
}

void MainWindow::configurePollForConnection()
{
    m_pollTimer->setInterval(isBleConnection() ? kPollIntervalBleMs : kPollIntervalUsbMs);
}

void MainWindow::updateSignalLabel(int rssiDbm, bool fromScan)
{
    if (!isBleConnection()
        && m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connecting) {
        m_signalLabel->setText(tr("n/a (USB)"));
        return;
    }
    const QString suffix = fromScan ? tr(" (scan)") : QString();
    m_signalLabel->setText(tr("%1 dBm%2").arg(rssiDbm).arg(suffix));
}

void MainWindow::startSlowStatusTimer()
{
    if (!m_slowStatusTimer) {
        return;
    }
    m_slowStatusTimer->start();
    // Soon after connect: pull DATA_BUF (may already contain Rare) + BLE RSSI.
    // Rare is not on-demand — only appears when the device has emitted a status record.
    QTimer::singleShot(1500, this, [this] {
        if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected
            && !m_spectrumInflight) {
            m_device->requestDataBuf();
            if (isBleConnection()) {
                m_device->requestBleRssi();
            }
        }
    });
    // Second pull a bit later — Rare is often slower than RealTime.
    QTimer::singleShot(8000, this, [this] {
        if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected
            && !m_spectrumInflight) {
            m_device->requestDataBuf();
        }
    });
}

void MainWindow::stopSlowStatusTimer()
{
    if (m_slowStatusTimer) {
        m_slowStatusTimer->stop();
    }
}

void MainWindow::onSlowStatusTick()
{
    if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
        return;
    }
    if (m_spectrumInflight) {
        return;
    }
    // Battery lives only in Rare DATA_BUF records (no dedicated VSFR). Poll the
    // stream so Rare is drained even when live poll is busy with spectrum/ROI.
    m_device->requestDataBuf();
    // BLE RSSI: best-effort on Qt 6.5+; no-op / silent on older stacks.
    if (isBleConnection()) {
        m_device->requestBleRssi();
    }
}

bool MainWindow::spectrumDwellDueMs(int dwellSeconds) const
{
    const qint64 dwellMs = qMax(1, dwellSeconds) * 1000LL;
    if (!m_haveSpectrumSample) {
        return true;
    }
    return m_sinceSpectrumSample.elapsed() >= dwellMs;
}

void MainWindow::pollData()
{
    if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
        return;
    }

    const bool ble = isBleConnection();
    const bool recording = m_roiPanel && m_roiPanel->isRecording();
    ++m_pollTick;

    if (ble) {
        // At most one command per 1 s tick so the queue never fills.
        // While a spectrum is in flight, wait (BLE spectrum can take many seconds).
        if (m_spectrumInflight) {
            return;
        }

        if (recording) {
            // Honor dwell in wall-clock time (not round-robin slots).
            const int dwell = qMax(1, m_roiPanel->dwellSeconds());
            if (spectrumDwellDueMs(dwell)) {
                requestSpectrumGated();
                return;
            }
            // Between samples: light live update, never stacked with spectrum.
            if (m_pollTick % 3 == 0) {
                m_device->requestTemperature();
            } else {
                m_device->requestDataBuf();
            }
            return;
        }

        // Live view (not recording): sparse round-robin.
        switch (m_pollTick % 6) {
        case 0:
        case 3:
            m_device->requestDataBuf();
            break;
        case 1:
        case 4:
            m_device->requestTemperature();
            break;
        default:
            // Spectrum about every 3 s when free (still gated).
            if (spectrumDwellDueMs(3)) {
                requestSpectrumGated();
            } else {
                m_device->requestDataBuf();
            }
            break;
        }
        return;
    }

    // USB: every 1 s dose/count (+ temperature); spectrum ~2 s or ROI dwell.
    m_device->requestDataBuf();
    m_device->requestTemperature();

    if (recording) {
        const int dwell = qMax(1, m_roiPanel->dwellSeconds());
        if (spectrumDwellDueMs(dwell)) {
            requestSpectrumGated();
        }
    } else if (m_pollTick % 2 == 0) {
        requestSpectrumGated();
    }
}

void MainWindow::onConnected()
{
    setConnectedUi(true);
    if (m_roiPanel) {
        m_roiPanel->setConnected(true);
    }
    m_serialLabel->setText(m_device->serialNumber());
    m_fwLabel->setText(m_device->firmwareVersion());
    m_statusLabel->setText(tr("Connected"));
    m_pollTick = 0;
    m_spectrumInflight = false;
    m_haveSpectrumSample = false;
    configurePollForConnection();

    if (isBleConnection()) {
        appendLog(tr("Connected %1 fw %2 via BLE — 1 cmd/s; ROI dwell is wall-clock "
                     "(spectrum may take longer than dwell over BLE)")
                      .arg(m_device->serialNumber(), m_device->firmwareVersion()));
        // Seed signal from last scan if we stored RSSI on the combo item.
        const int row = m_deviceCombo->currentIndex();
        if (row >= 0 && m_deviceCombo->itemData(row, RoleHasRssi).toBool()) {
            updateSignalLabel(m_deviceCombo->itemData(row, RoleRssi).toInt(), true);
        } else {
            m_signalLabel->setText(tr("…"));
        }
        // Single initial live sample; first spectrum on poll / after start recording.
        m_device->requestDataBuf();
        m_pollTimer->start();
        startSlowStatusTimer();
    } else {
        appendLog(tr("Connected %1 fw %2 — live rates 1 s, spectrum ~2 s (ROI dwell when recording)")
                      .arg(m_device->serialNumber(), m_device->firmwareVersion()));
        m_signalLabel->setText(tr("n/a (USB)"));
        m_device->requestDataBuf();
        m_device->requestTemperature();
        m_pollTimer->start();
        startSlowStatusTimer();
        QTimer::singleShot(300, this, [this] {
            if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected) {
                requestSpectrumGated();
            }
        });
    }
}

void MainWindow::onDisconnected()
{
    m_pollTimer->stop();
    stopSlowStatusTimer();
    m_pollTick = 0;
    m_spectrumInflight = false;
    m_haveSpectrumSample = false;
    setConnectedUi(false);
    if (m_roiPanel) {
        m_roiPanel->setConnected(false);
    }
    m_statusLabel->setText(tr("Disconnected"));
    m_doseLabel->setText(QStringLiteral("—"));
    m_countLabel->setText(QStringLiteral("—"));
    m_tempLabel->setText(QStringLiteral("—"));
    m_batteryLabel->setText(QStringLiteral("—"));
    m_signalLabel->setText(QStringLiteral("—"));
    m_spectrumLiveLabel->setText(QStringLiteral("—"));
    m_serialLabel->setText(QStringLiteral("—"));
    m_fwLabel->setText(QStringLiteral("—"));
    m_spectrum->clear();
    m_hasSpectrum = false;
    m_lastSpectrum = {};
    appendLog(tr("Disconnected"));
    refreshDeviceList();
}

void MainWindow::onError(const QString &message)
{
    using S = QtRadiacode::RadiaCodeDevice::State;
    const S st = m_device->state();

    // Free spectrum gate: queue-full means requestSpectrum was not enqueued;
    // transport failures never emit spectrumReady.
    if (st == S::Connected || st == S::Connecting) {
        m_spectrumInflight = false;
    }

    // Transient when poll outruns BLE/USB worker — do not spam the log.
    if (message.contains(QLatin1String("Command queue full"), Qt::CaseInsensitive)
        && (st == S::Connected || st == S::Connecting)) {
        return;
    }

    const QString line = tr("Error: %1").arg(message);
    appendLog(line);

    // A single failed USB op (e.g. one bulk timeout) must NOT stop live polling.
    // Only tear down UI when we are no longer in a live session.
    if (st == S::Connected || st == S::Connecting) {
        return;
    }

    m_pollTimer->stop();
    m_spectrumInflight = false;
    setConnectedUi(false);
    m_statusLabel->setText(tr("Error"));
    m_statusLabel->setToolTip(message);
}

void MainWindow::onStateChanged(QtRadiacode::RadiaCodeDevice::State state)
{
    using S = QtRadiacode::RadiaCodeDevice::State;
    switch (state) {
    case S::Disconnected:
        m_statusLabel->setText(tr("Disconnected"));
        if (!m_pollTimer->isActive()) {
            setConnectedUi(false);
        }
        break;
    case S::Connecting:
        m_statusLabel->setText(tr("Connecting…"));
        break;
    case S::Connected:
        m_statusLabel->setText(tr("Connected"));
        break;
    case S::Disconnecting:
        m_statusLabel->setText(tr("Disconnecting…"));
        break;
    case S::Error:
        m_statusLabel->setText(tr("Error"));
        setConnectedUi(false);
        break;
    }
}

void MainWindow::onDataBuf(const QList<QtRadiacode::RcDataItem> &items)
{
    // Prefer the latest RealTime / Rare sample in this batch.
    const QtRadiacode::RcRealTimeData *rt = nullptr;
    const QtRadiacode::RcRareData *rare = nullptr;
    for (const auto &item : items) {
        if (item.kind == QtRadiacode::RcDataKind::RealTime) {
            rt = &item.realTime;
        } else if (item.kind == QtRadiacode::RcDataKind::Rare) {
            rare = &item.rare;
        } else if (item.kind == QtRadiacode::RcDataKind::Raw) {
            // Fallback when device only pushed raw rates.
            m_countLabel->setText(QStringLiteral("%1 CPS").arg(item.raw.countRate, 0, 'f', 2));
            m_doseLabel->setText(QString::number(item.raw.doseRate, 'g', 6));
        }
    }
    if (rt) {
        m_countLabel->setText(
            QStringLiteral("%1 CPS (±%2%)")
                .arg(rt->countRate, 0, 'f', 2)
                .arg(rt->countRateErr, 0, 'f', 1));
        // Protocol dose_rate units match the Python library (device-native).
        m_doseLabel->setText(QString::number(rt->doseRate, 'g', 6));
    }
    if (rare) {
        m_tempLabel->setText(QStringLiteral("%1 °C").arg(rare->temperatureC, 0, 'f', 1));
        // chargeLevel is protocol % (Rare DATA_BUF); device sends Rare only periodically.
        m_batteryLabel->setText(tr("%1 %").arg(rare->chargeLevel, 0, 'f', 0));
        m_batteryLabel->setToolTip(
            tr("Last Rare DATA_BUF: dose accum %1 s, flags 0x%2")
                .arg(rare->durationSec)
                .arg(rare->flags, 0, 16));
    }
}

void MainWindow::onSpectrum(const QtRadiacode::RcSpectrum &sp)
{
    m_spectrumInflight = false;
    m_sinceSpectrumSample.restart();
    m_haveSpectrumSample = true;
    m_lastSpectrum = sp;
    m_hasSpectrum = !sp.counts.isEmpty();
    m_spectrum->setSpectrum(sp.counts, sp.a0, sp.a1, sp.a2);
    m_spectrumLiveLabel->setText(formatDuration(sp.durationSec));
    m_saveSpectrumBtn->setEnabled(m_hasSpectrum);
    if (m_roiPanel) {
        m_roiPanel->onSpectrum(sp);
    }

    quint64 totalCounts = 0;
    for (quint32 c : sp.counts) {
        totalCounts += c;
    }
    // ch = number of energy channels; total = sum of counts; live = accumulation time
    statusBar()->showMessage(
        tr("Spectrum: total %1 counts | %2 channels | live time %3")
            .arg(totalCounts)
            .arg(sp.counts.size())
            .arg(formatDuration(sp.durationSec)),
        3000);
}

void MainWindow::setConnectedUi(bool connected)
{
    m_connectBtn->setEnabled(!connected);
    m_disconnectBtn->setEnabled(connected);
    m_resetSpectrumBtn->setEnabled(connected);
    // Save uses last cached spectrum — allowed offline too after a capture.
    m_saveSpectrumBtn->setEnabled(m_hasSpectrum);
    m_deviceCombo->setEnabled(!connected);
    updateRefreshButton();
    if (m_roiPanel) {
        m_roiPanel->setConnected(connected);
    }
}

void MainWindow::appendLog(const QString &line)
{
    m_logLabel->setText(line);
    statusBar()->showMessage(line, 15000);
}
