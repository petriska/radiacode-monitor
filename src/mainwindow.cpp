#include "mainwindow.h"
#include "spectrumwidget.h"

#include "discovery/usbdiscovery.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Radiacode Monitor"));
    resize(900, 600);

    m_device = new QtRadiacode::RadiaCodeDevice(this);
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
    m_spectrumBtn = new QPushButton(tr("Spectrum"), this);
    connLay->addWidget(m_deviceCombo, 1);
    connLay->addWidget(m_refreshBtn);
    connLay->addWidget(m_connectBtn);
    connLay->addWidget(m_disconnectBtn);
    connLay->addWidget(m_spectrumBtn);
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
    form->addRow(tr("Status"), m_statusLabel);
    form->addRow(tr("Serial"), m_serialLabel);
    form->addRow(tr("Firmware"), m_fwLabel);
    form->addRow(tr("Dose rate (protocol units)"), m_doseLabel);
    form->addRow(tr("Count rate (CPS)"), m_countLabel);
    form->addRow(tr("Temperature"), m_tempLabel);
    root->addWidget(liveBox);

    m_spectrum = new SpectrumWidget(this);
    root->addWidget(m_spectrum, 1);

    m_logLabel = new QLabel(this);
    m_logLabel->setWordWrap(true);
    m_logLabel->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));
    root->addWidget(m_logLabel);

    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshDeviceList);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(m_spectrumBtn, &QPushButton::clicked, this, &MainWindow::onRefreshSpectrum);

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
    m_device->connectUsb(serial);
}

void MainWindow::onDisconnectClicked()
{
    m_device->disconnectFromDevice();
}

void MainWindow::onRefreshSpectrum()
{
    if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected) {
        m_device->requestSpectrum();
    }
}

void MainWindow::pollData()
{
    if (m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected) {
        m_device->requestDataBuf();
        m_device->requestTemperature();
    }
}

void MainWindow::onConnected()
{
    setConnectedUi(true);
    m_serialLabel->setText(m_device->serialNumber());
    m_fwLabel->setText(m_device->firmwareVersion());
    m_statusLabel->setText(tr("Connected"));
    appendLog(tr("Connected %1 fw %2")
                  .arg(m_device->serialNumber(), m_device->firmwareVersion()));
    m_pollTimer->start();
    m_device->requestSpectrum();
    pollData();
}

void MainWindow::onDisconnected()
{
    m_pollTimer->stop();
    setConnectedUi(false);
    m_statusLabel->setText(tr("Disconnected"));
    m_doseLabel->setText(QStringLiteral("—"));
    m_countLabel->setText(QStringLiteral("—"));
    m_spectrum->clear();
    appendLog(tr("Disconnected"));
}

void MainWindow::onError(const QString &message)
{
    appendLog(tr("Error: %1").arg(message));
    if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
        m_connectBtn->setEnabled(true);
    }
    // Non-modal status; use status bar-ish log only (avoid spam dialogs on poll errors)
}

void MainWindow::onStateChanged(QtRadiacode::RadiaCodeDevice::State state)
{
    using S = QtRadiacode::RadiaCodeDevice::State;
    switch (state) {
    case S::Disconnected:
        m_statusLabel->setText(tr("Disconnected"));
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
        break;
    }
}

void MainWindow::onDataBuf(const QList<QtRadiacode::RcDataItem> &items)
{
    for (const auto &item : items) {
        if (item.kind == QtRadiacode::RcDataKind::RealTime) {
            m_countLabel->setText(
                QStringLiteral("%1 CPS (±%2%)")
                    .arg(item.realTime.countRate, 0, 'f', 2)
                    .arg(item.realTime.countRateErr, 0, 'f', 1));
            // Protocol dose_rate is same units as Python (µR/h scale internally as float)
            m_doseLabel->setText(QString::number(item.realTime.doseRate, 'g', 6));
        } else if (item.kind == QtRadiacode::RcDataKind::Rare) {
            m_tempLabel->setText(
                QStringLiteral("%1 °C  charge %2%")
                    .arg(item.rare.temperatureC, 0, 'f', 1)
                    .arg(item.rare.chargeLevel, 0, 'f', 0));
        }
    }
}

void MainWindow::onSpectrum(const QtRadiacode::RcSpectrum &sp)
{
    m_spectrum->setSpectrum(sp.counts, sp.a0, sp.a1, sp.a2);
    appendLog(tr("Spectrum: %1 channels, live %2 s")
                  .arg(sp.counts.size())
                  .arg(sp.durationSec));
}

void MainWindow::setConnectedUi(bool connected)
{
    m_connectBtn->setEnabled(!connected);
    m_disconnectBtn->setEnabled(connected);
    m_spectrumBtn->setEnabled(connected);
    m_deviceCombo->setEnabled(!connected);
    m_refreshBtn->setEnabled(!connected);
}

void MainWindow::appendLog(const QString &line)
{
    m_logLabel->setText(line);
}
