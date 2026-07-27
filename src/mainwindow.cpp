#include "mainwindow.h"
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
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Radiacode Monitor"));
    resize(900, 600);

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

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);

    // Connection bar
    auto *connBox = new QGroupBox(tr("USB device"), this);
    auto *connLay = new QHBoxLayout(connBox);
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setMinimumWidth(280);
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
    m_refreshBtn->setToolTip(tr("Re-scan USB for Radiacode devices"));
    m_connectBtn->setToolTip(tr("Open USB connection to the selected device"));
    m_disconnectBtn->setToolTip(tr("Close USB connection and release the device"));
    connLay->addWidget(m_deviceCombo, 1);
    connLay->addWidget(m_refreshBtn);
    connLay->addWidget(m_connectBtn);
    connLay->addWidget(m_disconnectBtn);
    connLay->addWidget(m_resetSpectrumBtn);
    connLay->addWidget(m_saveSpectrumBtn);
    root->addWidget(connBox);

    // Live values
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
    form->addRow(tr("Status"), m_statusLabel);
    form->addRow(tr("Serial"), m_serialLabel);
    form->addRow(tr("Firmware"), m_fwLabel);
    form->addRow(tr("Dose rate (protocol units)"), m_doseLabel);
    form->addRow(tr("Count rate (CPS)"), m_countLabel);
    form->addRow(tr("Temperature"), m_tempLabel);
    form->addRow(tr("Spectrum live time"), m_spectrumLiveLabel);
    root->addWidget(liveBox);

    auto *tabs = new QTabWidget(this);
    m_spectrum = new SpectrumWidget(this);
    tabs->addTab(m_spectrum, tr("Spectrum"));

    m_roiPanel = new RoiTimeSeriesPanel(m_device, this);
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

    statusBar()->showMessage(tr("Ready — pick a USB device and Connect."));

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

    setConnectedUi(false);
    refreshDeviceList();
}

MainWindow::~MainWindow()
{
    m_pollTimer->stop();
    if (m_device && m_device->state() != QtRadiacode::RadiaCodeDevice::State::Disconnected) {
        m_device->disconnectFromDevice();
    }
}

void MainWindow::refreshDeviceList()
{
    const QString prev = m_deviceCombo->currentData().toString();
    m_deviceCombo->clear();
    const auto devices = QtRadiacode::listUsbDevices();
    for (const auto &d : devices) {
        const QString label = d.serialNumber.isEmpty()
            ? d.product
            : QStringLiteral("%1  (%2)").arg(d.serialNumber, d.product);
        m_deviceCombo->addItem(label, d.serialNumber);
    }
    if (devices.isEmpty()) {
        m_deviceCombo->addItem(tr("(no openable USB Radiacode — check udev)"), QString());
    }
    const int idx = m_deviceCombo->findData(prev);
    if (idx >= 0) {
        m_deviceCombo->setCurrentIndex(idx);
    }
    appendLog(tr("USB list: %1 device(s)").arg(devices.size()));
}

void MainWindow::onConnectClicked()
{
    const QString serial = m_deviceCombo->currentData().toString();
    appendLog(tr("Connecting USB %1…").arg(serial.isEmpty() ? tr("(first)") : serial));
    m_connectBtn->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_deviceCombo->setEnabled(false);
    m_device->connectUsb(serial);
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
    m_device->spectrumReset();
    // Next auto-poll will reload; also request once after a short delay.
    QTimer::singleShot(300, this, [this] {
        if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected) {
            m_device->requestSpectrum();
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

    const QString startDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    QFileDialog dlg(this, tr("Save spectrum"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setNameFilters(SpectrumExport::formatFilterString().split(QStringLiteral(";;")));
    dlg.selectNameFilter(QStringLiteral("CSV (*.csv)"));
    dlg.setDirectory(startDir);
    dlg.selectFile(baseName); // no extension in the name field
    dlg.setDefaultSuffix(SpectrumExport::defaultExtension(SpectrumExport::Format::Csv));
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

void MainWindow::pollData()
{
    if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
        return;
    }
    // Every 1 s: dose/count (+ temperature).
    m_device->requestDataBuf();
    m_device->requestTemperature();

    ++m_pollTick;
    if (m_roiPanel && m_roiPanel->isRecording()) {
        // ROI time series: spectrum on dwell boundary (poll is 1 s).
        const int dwell = qMax(1, m_roiPanel->dwellSeconds());
        if (m_pollTick % dwell == 0) {
            m_device->requestSpectrum();
        }
    } else if (m_pollTick % 2 == 0) {
        // Normal live view: spectrum about every 2 s.
        m_device->requestSpectrum();
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
    appendLog(tr("Connected %1 fw %2 — live rates 1 s, spectrum ~2 s (ROI dwell when recording)")
                  .arg(m_device->serialNumber(), m_device->firmwareVersion()));
    m_pollTick = 0;
    // First live sample immediately; first spectrum on the next even tick / shortly after.
    m_device->requestDataBuf();
    m_device->requestTemperature();
    m_pollTimer->start();
    QTimer::singleShot(300, this, [this] {
        if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected) {
            m_device->requestSpectrum();
        }
    });
}

void MainWindow::onDisconnected()
{
    m_pollTimer->stop();
    m_pollTick = 0;
    setConnectedUi(false);
    if (m_roiPanel) {
        m_roiPanel->setConnected(false);
    }
    m_statusLabel->setText(tr("Disconnected"));
    m_doseLabel->setText(QStringLiteral("—"));
    m_countLabel->setText(QStringLiteral("—"));
    m_tempLabel->setText(QStringLiteral("—"));
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
    const QString line = tr("Error: %1").arg(message);
    appendLog(line);

    using S = QtRadiacode::RadiaCodeDevice::State;
    const S st = m_device->state();

    // A single failed USB op (e.g. one bulk timeout) must NOT stop live polling.
    // Only tear down UI when we are no longer in a live session.
    if (st == S::Connected || st == S::Connecting) {
        return;
    }

    m_pollTimer->stop();
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
        m_tempLabel->setText(
            QStringLiteral("%1 °C  charge %2%")
                .arg(rare->temperatureC, 0, 'f', 1)
                .arg(rare->chargeLevel, 0, 'f', 0));
    }
}

void MainWindow::onSpectrum(const QtRadiacode::RcSpectrum &sp)
{
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
    m_refreshBtn->setEnabled(!connected);
    if (m_roiPanel) {
        m_roiPanel->setConnected(connected);
    }
}

void MainWindow::appendLog(const QString &line)
{
    m_logLabel->setText(line);
    statusBar()->showMessage(line, 15000);
}
