#include "mainwindow.h"
#include "roitimeseries/roimath.h"
#include "roitimeseries/roitimeseriespanel.h"
#include "spectrumexport.h"
#include "spectrogramfile.h"
#include "spectrogramrecorder.h"
#include "spectrumwaterfall.h"
#include "spectrumwidget.h"

#include "discovery/usbdiscovery.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {
// Update when the repo / downloads page URL changes.
constexpr char kReleasesUrl[] = "https://github.com/petriska/radiacode-monitor/releases";
constexpr char kRepoUrl[] = "https://github.com/petriska/radiacode-monitor";
constexpr char kLicenseUrl[] =
    "https://github.com/petriska/radiacode-monitor/blob/main/LICENSE";
constexpr char kThirdPartyUrl[] =
    "https://github.com/petriska/radiacode-monitor/blob/main/THIRD_PARTY.md";
constexpr char kQtRadiacodeUrl[] = "https://github.com/petriska/qtradiacode";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Radiacode Monitor"));
    resize(1100, 700);

    m_device = new QtRadiacode::RadiaCodeDevice(this);
    m_acquisition = new AcquisitionController(this);

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
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // --- Collapsible setup column (device / live / acquisition / ROI) ---
    m_setupToggleBtn = new QToolButton(this);
    m_setupToggleBtn->setAutoRaise(true);
    m_setupToggleBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_setupToggleBtn->setFixedWidth(22);
    m_setupToggleBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_setupToggleBtn->setToolTip(tr("Show or hide the setup panel (device, live, ROI)."));
    root->addWidget(m_setupToggleBtn, 0);

    m_setupPanel = new QWidget(this);
    auto *setupLay = new QVBoxLayout(m_setupPanel);
    setupLay->setContentsMargins(0, 0, 0, 0);
    setupLay->setSpacing(6);

    auto *liveBox = new QGroupBox(tr("Live"), m_setupPanel);
    auto *liveLay = new QVBoxLayout(liveBox);
    liveLay->setContentsMargins(8, 8, 8, 8);
    liveLay->setSpacing(4);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(3);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_statusLabel = new QLabel(tr("Disconnected"), this);
    // Kept off the main form (space); still updated for status tooltip / future UI.
    m_serialLabel = new QLabel(QStringLiteral("—"), this);
    m_fwLabel = new QLabel(QStringLiteral("—"), this);
    m_doseLabel = new QLabel(QStringLiteral("—"), this);
    m_serialLabel->setVisible(false);
    m_fwLabel->setVisible(false);
    m_doseLabel->setVisible(false);

    m_countLabel = new QLabel(QStringLiteral("—"), this);
    m_tempLabel = new QLabel(QStringLiteral("—"), this);
    m_spectrumLiveLabel = new QLabel(QStringLiteral("—"), this);
    m_spectrumTotalLabel = new QLabel(QStringLiteral("—"), this);
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
    form->addRow(tr("Count rate (CPS)"), m_countLabel);
    form->addRow(tr("Temperature"), m_tempLabel);
    form->addRow(tr("Battery"), m_batteryLabel);
    form->addRow(tr("BLE signal"), m_signalLabel);
    form->addRow(tr("Spectrum live time"), m_spectrumLiveLabel);
    form->addRow(tr("Spectrum total counts"), m_spectrumTotalLabel);

    m_resetSpectrumBtn = new QPushButton(tr("Reset spectrum"), this);
    m_resetSpectrumBtn->setToolTip(
        tr("Clear the spectrum accumulation on the device.\n"
           "The plot auto-refreshes about every 2 seconds while connected."));
    m_saveSpectrumBtn = new QPushButton(tr("Save spectrum…"), this);
    m_saveSpectrumBtn->setToolTip(
        tr("Save the spectrum currently shown (Live / Background / Net)\n"
           "as CSV, TKA, ANSI/IEEE N42.42, or NPES-JSON."));
    auto *spectrumActions = new QHBoxLayout;
    spectrumActions->setSpacing(6);
    spectrumActions->addWidget(m_resetSpectrumBtn);
    spectrumActions->addWidget(m_saveSpectrumBtn);
    spectrumActions->addStretch(1);
    form->addRow(QString(), spectrumActions);

    m_spectrumViewCombo = new QComboBox(this);
    m_spectrumViewCombo->addItem(tr("Live"), int(SpectrumView::Live));
    m_spectrumViewCombo->addItem(tr("Background"), int(SpectrumView::Background));
    m_spectrumViewCombo->addItem(tr("Net (Live − BG)"), int(SpectrumView::Net));
    m_spectrumViewCombo->setToolTip(
        tr("What the spectrum plot shows (enabled after a background is loaded).\n"
           "Live: current device spectrum.\n"
           "Background: loaded BG spectrum.\n"
           "Net: Live − BG scaled by live-time ratio (negative bins → 0).\n\n"
           "Workflow: Save spectrum → Load BG… → choose Background or Net."));
    m_spectrumViewCombo->setEnabled(false);
    m_loadBgBtn = new QPushButton(tr("Load BG…"), this);
    m_loadBgBtn->setToolTip(
        tr("Load a background spectrum from file (CSV, TKA, N42, NPES-JSON).\n"
           "Save a spectrum first if you want to reuse a measurement as BG."));
    m_bgStatusLabel = new QLabel(tr("BG: none — Load BG… to enable view modes"), this);
    m_bgStatusLabel->setStyleSheet(QStringLiteral("color: #aaa;"));
    m_bgStatusLabel->setWordWrap(true);
    auto *bgRow = new QHBoxLayout;
    bgRow->setSpacing(6);
    bgRow->addWidget(m_spectrumViewCombo, 1);
    bgRow->addWidget(m_loadBgBtn);
    form->addRow(tr("View"), bgRow);
    form->addRow(QString(), m_bgStatusLabel);

    m_waterfallIntegrateSpin = new QSpinBox(this);
    m_waterfallIntegrateSpin->setRange(1, 32);
    m_waterfallIntegrateSpin->setValue(1);
    m_waterfallIntegrateSpin->setSuffix(tr(" polls"));
    m_waterfallIntegrateSpin->setToolTip(
        tr("Spectrogram integrate: each display row sums N spectrum updates.\n"
           "1 = every poll (~1 s per row). Higher N = coarser time, more wall-clock\n"
           "history for the same row budget."));
    form->addRow(tr("Waterfall integrate"), m_waterfallIntegrateSpin);

    m_waterfallHistoryCombo = new QComboBox(this);
    m_waterfallHistoryCombo->addItem(tr("15 min"), 15);
    m_waterfallHistoryCombo->addItem(tr("30 min"), 30);
    m_waterfallHistoryCombo->addItem(tr("1 hour"), 60);
    m_waterfallHistoryCombo->addItem(tr("2 hours"), 120);
    m_waterfallHistoryCombo->addItem(tr("4 hours"), 240);
    m_waterfallHistoryCombo->setCurrentIndex(3); // 2 h default
    m_waterfallHistoryCombo->setToolTip(
        tr("How much spectrogram history to keep in memory.\n"
           "Row count ≈ minutes × 60 / integrate (1 s polls).\n"
           "Wheel over the waterfall scrolls history; double-click returns to live."));
    form->addRow(tr("Waterfall history"), m_waterfallHistoryCombo);

    m_waterfallLiveBtn = new QPushButton(tr("Follow live"), this);
    m_waterfallLiveBtn->setEnabled(false);
    m_waterfallLiveBtn->setToolTip(
        tr("Jump the spectrogram viewport to the newest data (also: double-click waterfall)."));
    form->addRow(QString(), m_waterfallLiveBtn);

    m_recordSpectrogramCheck = new QCheckBox(tr("Record continuously"), this);
    m_recordSpectrogramCheck->setToolTip(
        tr("Append every spectrogram row to a daily .rcsg file on disk\n"
           "(…/spectrograms/<serial>/YYYY-MM-DD.rcsg).\n"
           "Same day: append after restart. Past days: gzip → .rcsg.gz; old files pruned."));
    form->addRow(QString(), m_recordSpectrogramCheck);

    m_recordFolderBtn = new QPushButton(tr("Recording folder…"), this);
    m_recordFolderBtn->setToolTip(tr("Choose the base directory for continuous spectrogram files."));
    form->addRow(QString(), m_recordFolderBtn);

    m_recordKeepDaysCombo = new QComboBox(this);
    m_recordKeepDaysCombo->addItem(tr("7 days"), 7);
    m_recordKeepDaysCombo->addItem(tr("14 days"), 14);
    m_recordKeepDaysCombo->addItem(tr("30 days"), 30);
    m_recordKeepDaysCombo->addItem(tr("90 days"), 90);
    m_recordKeepDaysCombo->addItem(tr("1 year"), 365);
    m_recordKeepDaysCombo->setCurrentIndex(2); // 30
    m_recordKeepDaysCombo->setToolTip(
        tr("Delete spectrogram day files older than this (raw .rcsg and .rcsg.gz).\n"
           "When a day rolls over, the closed file is gzip-compressed."));
    form->addRow(tr("Keep recordings"), m_recordKeepDaysCombo);

    m_recordStatusLabel = new QLabel(tr("Recording: off"), this);
    m_recordStatusLabel->setWordWrap(true);
    m_recordStatusLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    form->addRow(QString(), m_recordStatusLabel);

    liveLay->addLayout(form);

    // Acquisition run: stop by device live time or total spectrum counts.
    auto *acqBox = new QGroupBox(tr("Acquisition"), this);
    auto *acqLay = new QVBoxLayout(acqBox);
    acqLay->setContentsMargins(6, 6, 6, 6);
    acqLay->setSpacing(4);
    auto *acqModeRow = new QHBoxLayout;
    acqModeRow->setSpacing(6);
    m_acqModeCombo = new QComboBox(this);
    m_acqModeCombo->addItem(tr("Time (live s)"),
                            int(AcquisitionController::Mode::TimeSeconds));
    m_acqModeCombo->addItem(tr("Total counts"),
                            int(AcquisitionController::Mode::TotalCounts));
    m_acqModeCombo->setToolTip(
        tr("Stop condition for a measurement run.\n"
           "Time uses the device spectrum live time (not wall clock).\n"
           "Total counts = sum of all spectrum channels.\n"
           "Stop is checked after each full spectrum (± one poll frame)."));
    m_acqTargetSpin = new QSpinBox(this);
    m_acqTargetSpin->setRange(1, 2000000000);
    m_acqTargetSpin->setValue(300);
    m_acqTargetSpin->setToolTip(tr("Target live time in seconds, or total counts."));
    acqModeRow->addWidget(m_acqModeCombo, 1);
    acqModeRow->addWidget(m_acqTargetSpin);
    acqLay->addLayout(acqModeRow);

    auto *acqBtnRow = new QHBoxLayout;
    acqBtnRow->setSpacing(6);
    m_acqStartBtn = new QPushButton(tr("Start"), this);
    m_acqStopBtn = new QPushButton(tr("Stop"), this);
    m_acqStartBtn->setToolTip(
        tr("Start acquisition until the target is reached.\n"
           "If the spectrum already has data, you can Continue, Reset and start, "
           "or Save first."));
    m_acqStopBtn->setToolTip(tr("Abort the current acquisition run"));
    acqBtnRow->addWidget(m_acqStartBtn);
    acqBtnRow->addWidget(m_acqStopBtn);
    acqBtnRow->addStretch(1);
    acqLay->addLayout(acqBtnRow);

    m_acqProgressBar = new QProgressBar(this);
    m_acqProgressBar->setRange(0, 1000); // 0.1 % resolution
    m_acqProgressBar->setValue(0);
    m_acqProgressBar->setTextVisible(true);
    m_acqProgressBar->setFormat(tr("%p%"));
    m_acqProgressBar->setMinimumHeight(16);
    m_acqProgressBar->setMaximumHeight(18);
    m_acqProgressBar->setToolTip(
        tr("Acquisition progress toward the time or count target."));
    acqLay->addWidget(m_acqProgressBar);

    m_acqProgressLabel = new QLabel(tr("—"), this);
    m_acqProgressLabel->setWordWrap(true);
    m_acqProgressLabel->setStyleSheet(QStringLiteral("color: #bbb;"));
    acqLay->addWidget(m_acqProgressLabel);
    liveLay->addWidget(acqBox);
    liveBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *connBox = new QGroupBox(tr("Device"), m_setupPanel);
    auto *connLay = new QVBoxLayout(connBox);
    connLay->setContentsMargins(8, 6, 8, 6);
    connLay->setSpacing(4);
    auto *connBtnRow = new QHBoxLayout;
    connBtnRow->setSpacing(6);
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setMinimumWidth(200);
    m_deviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_refreshBtn = new QPushButton(tr("Refresh"), this);
    m_connectBtn = new QPushButton(tr("Connect"), this);
    m_disconnectBtn = new QPushButton(tr("Disconnect"), this);
    m_refreshBtn->setToolTip(
        tr("Re-scan USB and BLE for Radiacode devices.\n"
           "BLE: device must be free (not held by phone or Home Assistant)."));
    m_connectBtn->setToolTip(tr("Connect via USB or BLE to the selected device"));
    m_disconnectBtn->setToolTip(tr("Close connection and release the device"));
    connBtnRow->addWidget(m_deviceCombo, 1);
    connBtnRow->addWidget(m_refreshBtn);
    connBtnRow->addWidget(m_connectBtn);
    connBtnRow->addWidget(m_disconnectBtn);
    connLay->addLayout(connBtnRow);
    // Serial / firmware live here instead of the Live form (compact one line).
    auto *deviceMeta = new QHBoxLayout;
    deviceMeta->setSpacing(12);
    m_serialLabel->setVisible(true);
    m_fwLabel->setVisible(true);
    m_serialLabel->setStyleSheet(QStringLiteral("color: #aaa;"));
    m_fwLabel->setStyleSheet(QStringLiteral("color: #aaa;"));
    m_serialLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fwLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    deviceMeta->addWidget(new QLabel(tr("Serial:"), this));
    deviceMeta->addWidget(m_serialLabel, 1);
    deviceMeta->addWidget(new QLabel(tr("FW:"), this));
    deviceMeta->addWidget(m_fwLabel, 1);
    connLay->addLayout(deviceMeta);
    connBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    // Setup stack: Device on top, Live+Acquisition, then ROI controls (scrollable).
    setupLay->addWidget(connBox);
    setupLay->addWidget(liveBox);

    m_roiPanel = new RoiTimeSeriesPanel(m_device, this);
    if (QWidget *roiCtrl = m_roiPanel->controlsWidget()) {
        roiCtrl->setMinimumWidth(260);
        roiCtrl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        auto *roiBox = new QGroupBox(tr("ROI"), m_setupPanel);
        auto *roiLay = new QVBoxLayout(roiBox);
        roiLay->setContentsMargins(6, 6, 6, 6);
        roiLay->addWidget(roiCtrl);
        setupLay->addWidget(roiBox, 1);
    } else {
        setupLay->addStretch(1);
    }

    m_setupPanel->setMinimumWidth(280);
    m_setupPanel->setMaximumWidth(400);
    m_setupPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *setupScroll = new QScrollArea(this);
    setupScroll->setWidget(m_setupPanel);
    setupScroll->setWidgetResizable(true);
    setupScroll->setFrameShape(QFrame::NoFrame);
    setupScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setupScroll->setMinimumWidth(0);
    setupScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    // --- Main plots (dominant) ---
    auto *tabs = new QTabWidget(this);
    m_spectrum = new SpectrumWidget(this);
    m_waterfall = new SpectrumWaterfall(this);
    m_spectrogramRecorder = new SpectrogramRecorder(this);
    m_waterfall->setRecorder(m_spectrogramRecorder);
    m_spectrumSplit = new QSplitter(Qt::Vertical, this);
    m_spectrumSplit->setChildrenCollapsible(false);
    m_spectrumSplit->addWidget(m_spectrum);
    m_spectrumSplit->addWidget(m_waterfall);
    m_spectrumSplit->setStretchFactor(0, 1);
    m_spectrumSplit->setStretchFactor(1, 3);
    m_spectrumSplit->setSizes({180, 480});
    tabs->addTab(m_spectrumSplit, tr("Spectrum"));
    // ROI tab: chart only (controls live in the setup panel).
    tabs->addTab(m_roiPanel, tr("ROI time series"));

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setChildrenCollapsible(true);
    m_mainSplitter->addWidget(setupScroll);
    m_mainSplitter->addWidget(tabs);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({320, 900});
    root->addWidget(m_mainSplitter, 1);

    connect(m_setupToggleBtn, &QToolButton::clicked, this, [this] {
        setSetupPanelVisible(!m_setupVisible);
    });

    connect(m_spectrum, &SpectrumWidget::viewRangeChanged, m_waterfall,
            &SpectrumWaterfall::setViewRange);
    // Cross-link cursors: hover on one highlights the same channel on the other.
    connect(m_spectrum, &SpectrumWidget::cursorInfoChanged, this,
            [this](int channel, double energyKeV, quint32 counts) {
        if (m_waterfall) {
            m_waterfall->setLinkedChannel(channel);
        }
        if (channel < 0) {
            return;
        }
        if (qAbs(m_lastSpectrum.a1) > 1e-12f || qAbs(m_lastSpectrum.a2) > 1e-12f
            || qAbs(m_lastSpectrum.a0) > 1e-12f) {
            statusBar()->showMessage(
                tr("Spectrum: E = %1 keV · ch %2 · N = %3")
                    .arg(energyKeV, 0, 'f', 1)
                    .arg(channel)
                    .arg(counts),
                2000);
        } else {
            statusBar()->showMessage(
                tr("Spectrum: ch %1 · N = %2").arg(channel).arg(counts), 2000);
        }
    });
    connect(m_waterfall, &SpectrumWaterfall::cursorInfoChanged, this,
            [this](int channel, double energyKeV, float rateCps, quint32 deltaCounts,
                   quint32 liveTimeSec, int ageFromNewestSec) {
        if (m_spectrum) {
            m_spectrum->setLinkedChannel(channel);
        }
        if (channel < 0) {
            return;
        }
        if (qAbs(m_lastSpectrum.a1) > 1e-12f || qAbs(m_lastSpectrum.a2) > 1e-12f
            || qAbs(m_lastSpectrum.a0) > 1e-12f) {
            statusBar()->showMessage(
                tr("Waterfall: E = %1 keV · ch %2 · %3 cps · ΔN = %4 · live %5 s · t−%6 s")
                    .arg(energyKeV, 0, 'f', 1)
                    .arg(channel)
                    .arg(rateCps, 0, 'f', 2)
                    .arg(deltaCounts)
                    .arg(liveTimeSec)
                    .arg(ageFromNewestSec),
                2500);
        } else {
            statusBar()->showMessage(
                tr("Waterfall: ch %1 · %2 cps · ΔN = %3 · live %4 s · t−%5 s")
                    .arg(channel)
                    .arg(rateCps, 0, 'f', 2)
                    .arg(deltaCounts)
                    .arg(liveTimeSec)
                    .arg(ageFromNewestSec),
                2500);
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
        if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
            return;
        }

        const bool acqWaiting =
            m_acquisition
            && m_acquisition->state() == AcquisitionController::State::WaitingReset;
        const bool roiRecording = m_roiPanel && m_roiPanel->isRecording();

        if (acqWaiting) {
            m_acquisition->notifySpectrumResetFinished();
        }
        if (!acqWaiting && !roiRecording) {
            return;
        }
        // Brief settle after WR_VIRT_STRING spectrum clear, then first sample.
        QTimer::singleShot(150, this, [this, acqWaiting, roiRecording] {
            if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
                return;
            }
            if (acqWaiting
                || (roiRecording && m_roiPanel && m_roiPanel->isRecording())) {
                m_device->requestSpectrum();
            }
        });
    });

    statusBar()->showMessage(tr("Ready — pick a device and Connect."));

    m_bleScanner = new QtRadiacode::RcBleScanner(this);

    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshDeviceList);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(m_resetSpectrumBtn, &QPushButton::clicked, this, &MainWindow::onResetSpectrum);
    connect(m_saveSpectrumBtn, &QPushButton::clicked, this, &MainWindow::onSaveSpectrum);
    connect(m_loadBgBtn, &QPushButton::clicked, this, &MainWindow::onLoadBackground);
    connect(m_spectrumViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onSpectrumViewChanged);
    connect(m_waterfallIntegrateSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int n) {
        if (m_waterfall) {
            m_waterfall->setIntegrateCount(n);
        }
        QSettings().setValue(QStringLiteral("waterfall/integrate"), n);
    });
    connect(m_waterfallHistoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
        if (!m_waterfallHistoryCombo || !m_waterfall) {
            return;
        }
        const int minutes = m_waterfallHistoryCombo->currentData().toInt();
        m_waterfall->setHistoryMinutes(minutes);
        QSettings().setValue(QStringLiteral("waterfall/historyMinutes"), minutes);
    });
    connect(m_waterfallLiveBtn, &QPushButton::clicked, this, [this] {
        if (m_waterfall) {
            m_waterfall->followLive();
        }
    });
    connect(m_waterfall, &SpectrumWaterfall::followLiveChanged, this, [this](bool following) {
        if (m_waterfallLiveBtn) {
            m_waterfallLiveBtn->setEnabled(!following);
        }
    });
    connect(m_recordSpectrogramCheck, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("waterfall/recordContinuous"), on);
        syncSpectrogramRecording();
    });
    connect(m_recordFolderBtn, &QPushButton::clicked, this, &MainWindow::onChooseRecordFolder);
    connect(m_recordKeepDaysCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
        if (!m_recordKeepDaysCombo || !m_spectrogramRecorder) {
            return;
        }
        const int days = m_recordKeepDaysCombo->currentData().toInt();
        m_spectrogramRecorder->setKeepDays(days);
        QSettings().setValue(QStringLiteral("waterfall/recordKeepDays"), days);
        m_spectrogramRecorder->pruneOldFiles();
    });
    connect(m_spectrogramRecorder, &SpectrogramRecorder::recordingChanged, this, [this](bool on) {
        if (m_recordStatusLabel) {
            if (!on) {
                m_recordStatusLabel->setText(tr("Recording: off"));
            }
        }
    });
    connect(m_spectrogramRecorder, &SpectrogramRecorder::pathChanged, this, [this](const QString &path) {
        if (!m_recordStatusLabel) {
            return;
        }
        if (path.isEmpty()) {
            if (m_spectrogramRecorder && !m_spectrogramRecorder->isRecording()) {
                m_recordStatusLabel->setText(tr("Recording: off"));
            }
            return;
        }
        m_recordStatusLabel->setText(tr("Recording → %1").arg(path));
        m_recordStatusLabel->setToolTip(path);
    });
    connect(m_spectrogramRecorder, &SpectrogramRecorder::errorOccurred, this,
            [this](const QString &msg) {
        statusBar()->showMessage(tr("Spectrogram record: %1").arg(msg), 8000);
        appendLog(tr("Spectrogram record error: %1").arg(msg));
    });
    connect(m_acqStartBtn, &QPushButton::clicked, this, &MainWindow::onAcquisitionStart);
    connect(m_acqStopBtn, &QPushButton::clicked, this, &MainWindow::onAcquisitionStop);
    connect(m_acqModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onAcquisitionModeChanged);

    connect(m_acquisition, &AcquisitionController::requestSpectrumReset, this, [this] {
        if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
            abortAcquisitionIfActive(tr("not connected"));
            return;
        }
        m_device->spectrumReset();
    });
    connect(m_acquisition, &AcquisitionController::logMessage, this, &MainWindow::appendLog);
    connect(m_acquisition, &AcquisitionController::progress, this,
            [this](double fraction, const QString &text) {
        if (m_acqProgressBar) {
            const int v = qBound(0, int(fraction * 1000.0 + 0.5), 1000);
            m_acqProgressBar->setValue(v);
        }
        if (m_acqProgressLabel) {
            m_acqProgressLabel->setText(text);
        }
    });
    connect(m_acquisition, &AcquisitionController::stateChanged, this,
            [this](AcquisitionController::State) { updateAcquisitionUi(); });
    connect(m_acquisition, &AcquisitionController::completed, this, [this](const QString &summary) {
        statusBar()->showMessage(tr("Acquisition %1").arg(summary), 15000);
        updateAcquisitionUi();
    });
    connect(m_acquisition, &AcquisitionController::aborted, this,
            [this](const QString &) { updateAcquisitionUi(); });

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

    // After ROI panel exists (File → Export ROI CSV…).
    setupMenuBar();

    onAcquisitionModeChanged();
    setConnectedUi(false);
    updateAcquisitionUi();
    updateBackgroundUi();
    {
        QSettings settings;
        const int integ = settings.value(QStringLiteral("waterfall/integrate"), 1).toInt();
        if (m_waterfallIntegrateSpin) {
            const QSignalBlocker blocker(m_waterfallIntegrateSpin);
            m_waterfallIntegrateSpin->setValue(qBound(1, integ, 32));
        }
        const int histMin =
            settings.value(QStringLiteral("waterfall/historyMinutes"), 120).toInt();
        if (m_waterfallHistoryCombo) {
            const QSignalBlocker blocker(m_waterfallHistoryCombo);
            int idx = m_waterfallHistoryCombo->findData(histMin);
            if (idx < 0) {
                // Nearest lower preset.
                idx = 0;
                for (int i = 0; i < m_waterfallHistoryCombo->count(); ++i) {
                    if (m_waterfallHistoryCombo->itemData(i).toInt() <= histMin) {
                        idx = i;
                    }
                }
            }
            m_waterfallHistoryCombo->setCurrentIndex(idx);
        }
        if (m_waterfall) {
            m_waterfall->setIntegrateCount(m_waterfallIntegrateSpin
                                               ? m_waterfallIntegrateSpin->value()
                                               : 1);
            m_waterfall->setHistoryMinutes(
                m_waterfallHistoryCombo ? m_waterfallHistoryCombo->currentData().toInt()
                                        : 120);
        }
        const bool rec =
            settings.value(QStringLiteral("waterfall/recordContinuous"), false).toBool();
        if (m_recordSpectrogramCheck) {
            const QSignalBlocker b(m_recordSpectrogramCheck);
            m_recordSpectrogramCheck->setChecked(rec);
        }
        QString recDir = settings.value(QStringLiteral("waterfall/recordDir")).toString();
        if (recDir.isEmpty()) {
            recDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                + QStringLiteral("/RadiacodeMonitor/spectrograms");
        }
        const int keepDays = settings.value(QStringLiteral("waterfall/recordKeepDays"), 30).toInt();
        if (m_recordKeepDaysCombo) {
            const QSignalBlocker b(m_recordKeepDaysCombo);
            int idx = m_recordKeepDaysCombo->findData(keepDays);
            if (idx < 0) {
                idx = 2;
            }
            m_recordKeepDaysCombo->setCurrentIndex(idx);
        }
        if (m_spectrogramRecorder) {
            m_spectrogramRecorder->setBaseDirectory(recDir);
            m_spectrogramRecorder->setKeepDays(
                m_recordKeepDaysCombo ? m_recordKeepDaysCombo->currentData().toInt() : 30);
            m_spectrogramRecorder->setCompressOnRoll(true);
        }

        const bool setupVis = settings.value(QStringLiteral("ui/setupVisible"), true).toBool();
        setSetupPanelVisible(setupVis);
        const QByteArray mainState =
            settings.value(QStringLiteral("ui/mainSplitter")).toByteArray();
        if (m_mainSplitter && !mainState.isEmpty()) {
            m_mainSplitter->restoreState(mainState);
        }
        const QByteArray specState =
            settings.value(QStringLiteral("ui/spectrumSplitter")).toByteArray();
        if (m_spectrumSplit && !specState.isEmpty()) {
            m_spectrumSplit->restoreState(specState);
        }
    }
    updateSetupToggleUi();
    refreshDeviceList();
}

MainWindow::~MainWindow()
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/setupVisible"), m_setupVisible);
    if (m_mainSplitter) {
        settings.setValue(QStringLiteral("ui/mainSplitter"), m_mainSplitter->saveState());
    }
    if (m_spectrumSplit) {
        settings.setValue(QStringLiteral("ui/spectrumSplitter"), m_spectrumSplit->saveState());
    }
    if (m_spectrogramRecorder) {
        m_spectrogramRecorder->stop();
    }
    m_pollTimer->stop();
    stopBleScan();
    if (m_device && m_device->state() != QtRadiacode::RadiaCodeDevice::State::Disconnected) {
        m_device->disconnectFromDevice();
    }
}

void MainWindow::updateSetupToggleUi()
{
    if (!m_setupToggleBtn) {
        return;
    }
    if (m_setupVisible) {
        m_setupToggleBtn->setText(QStringLiteral("‹"));
        m_setupToggleBtn->setToolTip(tr("Hide setup panel"));
    } else {
        m_setupToggleBtn->setText(QStringLiteral("›"));
        m_setupToggleBtn->setToolTip(tr("Show setup panel (device, live, ROI)"));
    }
    if (m_toggleSetupAct) {
        m_toggleSetupAct->setChecked(m_setupVisible);
    }
}

void MainWindow::setSetupPanelVisible(bool visible)
{
    if (!m_mainSplitter || m_mainSplitter->count() < 2) {
        m_setupVisible = visible;
        updateSetupToggleUi();
        return;
    }

    QWidget *setupSide = m_mainSplitter->widget(0);
    if (!setupSide) {
        return;
    }

    if (visible == m_setupVisible && setupSide->isVisible() == visible) {
        updateSetupToggleUi();
        return;
    }

    if (!visible && m_setupVisible) {
        m_savedMainSizes = m_mainSplitter->sizes();
    }

    m_setupVisible = visible;
    setupSide->setVisible(visible);

    if (visible) {
        if (m_savedMainSizes.size() >= 2 && m_savedMainSizes[0] > 40) {
            m_mainSplitter->setSizes(m_savedMainSizes);
        } else {
            const int total = m_mainSplitter->width() > 0 ? m_mainSplitter->width() : 1200;
            m_mainSplitter->setSizes({320, qMax(400, total - 320)});
        }
    } else {
        const int total = qMax(1, m_mainSplitter->width());
        m_mainSplitter->setSizes({0, total});
    }

    QSettings().setValue(QStringLiteral("ui/setupVisible"), m_setupVisible);
    updateSetupToggleUi();
}

void MainWindow::onChooseRecordFolder()
{
    QString start = m_spectrogramRecorder ? m_spectrogramRecorder->baseDirectory() : QString();
    if (start.isEmpty()) {
        start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Spectrogram recording folder"), start);
    if (dir.isEmpty()) {
        return;
    }
    QSettings().setValue(QStringLiteral("waterfall/recordDir"), dir);
    if (m_spectrogramRecorder) {
        const bool was = m_spectrogramRecorder->isRecording();
        m_spectrogramRecorder->stop();
        m_spectrogramRecorder->setBaseDirectory(dir);
        if (was || (m_recordSpectrogramCheck && m_recordSpectrogramCheck->isChecked())) {
            syncSpectrogramRecording();
        }
    }
    statusBar()->showMessage(tr("Spectrogram folder: %1").arg(dir), 5000);
}

void MainWindow::syncSpectrogramRecording()
{
    if (!m_spectrogramRecorder || !m_waterfall) {
        return;
    }

    const bool want = m_recordSpectrogramCheck && m_recordSpectrogramCheck->isChecked();
    if (!want) {
        m_spectrogramRecorder->stop();
        return;
    }

    // Need connected device + known channel count (after first spectrum).
    if (!m_device || m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
        m_spectrogramRecorder->stop();
        if (m_recordStatusLabel) {
            m_recordStatusLabel->setText(tr("Recording: waiting for connect…"));
        }
        return;
    }
    if (!m_hasSpectrum || m_lastSpectrum.counts.isEmpty()) {
        m_spectrogramRecorder->stop();
        if (m_recordStatusLabel) {
            m_recordStatusLabel->setText(tr("Recording: waiting for spectrum…"));
        }
        return;
    }

    SpectrogramFile::Header h;
    h.nChannels = quint32(m_lastSpectrum.counts.size());
    h.a0 = m_lastSpectrum.a0;
    h.a1 = m_lastSpectrum.a1;
    h.a2 = m_lastSpectrum.a2;
    h.integrate = quint32(m_waterfall->integrateCount());
    h.historyMinutes = quint32(m_waterfall->historyMinutes());
    h.serial = m_device->serialNumber();
    h.flags = SpectrogramFile::kFlagHasDeltas;

    if (m_spectrogramRecorder->baseDirectory().isEmpty()) {
        const QString def =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/RadiacodeMonitor/spectrograms");
        m_spectrogramRecorder->setBaseDirectory(def);
    }

    QString err;
    if (!m_spectrogramRecorder->start(h, &err)) {
        if (m_recordStatusLabel) {
            m_recordStatusLabel->setText(tr("Recording: error"));
            m_recordStatusLabel->setToolTip(err);
        }
        statusBar()->showMessage(tr("Spectrogram record: %1").arg(err), 8000);
        return;
    }
}

void MainWindow::setupMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));

    m_saveSpectrumAct = fileMenu->addAction(tr("&Save Spectrum…"), this,
                                            &MainWindow::onSaveSpectrum);
    m_saveSpectrumAct->setShortcut(QKeySequence::Save);
    m_saveSpectrumAct->setToolTip(
        tr("Save the last spectrum as CSV, TKA, ANSI/IEEE N42.42, or NPES-JSON."));
    m_saveSpectrumAct->setEnabled(false);

    m_exportRoiCsvAct = fileMenu->addAction(tr("Export ROI &CSV…"), this, [this] {
        if (m_roiPanel) {
            m_roiPanel->exportCsv();
        }
    });
    m_exportRoiCsvAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    m_exportRoiCsvAct->setToolTip(
        tr("Export recorded ROI time series samples to a CSV file."));

    fileMenu->addSeparator();
    auto *saveSpecAct = fileMenu->addAction(tr("Save Spectrogram &History…"), this, [this] {
        if (m_waterfall) {
            m_waterfall->saveHistory(this);
        }
    });
    saveSpecAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+H")));
    saveSpecAct->setToolTip(
        tr("Save the waterfall spectrogram buffer (.rcsg): rates, timestamps, serial."));

    auto *loadSpecAct = fileMenu->addAction(tr("&Load Spectrogram History…"), this, [this] {
        if (m_waterfall) {
            m_waterfall->loadHistory(this);
        }
    });
    loadSpecAct->setToolTip(
        tr("Load a .rcsg spectrogram into the waterfall (replaces current history)."));

    fileMenu->addSeparator();
    auto *exitAct = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAct->setShortcut(QKeySequence::Quit);
    exitAct->setMenuRole(QAction::QuitRole);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    m_toggleSetupAct = viewMenu->addAction(tr("Show &setup panel"));
    m_toggleSetupAct->setCheckable(true);
    m_toggleSetupAct->setChecked(m_setupVisible);
    m_toggleSetupAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    m_toggleSetupAct->setToolTip(tr("Show or hide device / live / ROI settings."));
    connect(m_toggleSetupAct, &QAction::toggled, this, [this](bool on) {
        setSetupPanelVisible(on);
    });

    m_focusSpectrogramAct = viewMenu->addAction(tr("&Focus spectrogram"), this, [this] {
        // Hide setup and give waterfall most of the plot height.
        setSetupPanelVisible(false);
        if (m_spectrumSplit) {
            m_savedSpectrumSizes = m_spectrumSplit->sizes();
            const int h = qMax(200, m_spectrumSplit->height());
            m_spectrumSplit->setSizes({qMax(80, h / 5), qMax(120, (h * 4) / 5)});
        }
    });
    m_focusSpectrogramAct->setShortcut(QKeySequence(QStringLiteral("F11")));
    m_focusSpectrogramAct->setToolTip(
        tr("Hide setup panel and enlarge the spectrogram (waterfall)."));

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAct = helpMenu->addAction(tr("&About Radiacode Monitor…"), this,
                                         &MainWindow::onAbout);
    aboutAct->setMenuRole(QAction::AboutRole);
    auto *aboutQtAct = helpMenu->addAction(tr("About &Qt…"), this, &MainWindow::onAboutQt);
    aboutQtAct->setMenuRole(QAction::AboutQtRole);
}

void MainWindow::onAbout()
{
    const QString appVersion = QApplication::applicationVersion();
    const QString qtRuntime = QString::fromLatin1(qVersion());
    const QString qtBuild = QStringLiteral(QT_VERSION_STR);

    // Rich text so license / release links open in the browser.
    const QString text = tr(
        "<h3>Radiacode Monitor</h3>"
        "<p>Version <b>%1</b></p>"
        "<p>Desktop monitor for RadiaCode spectrometers (USB and BLE), "
        "built with <a href=\"%2\">QtRadiacode</a> and Qt.</p>"
        "<p><i>Unofficial community software. Not affiliated with, endorsed by, "
        "or sponsored by the RadiaCode hardware manufacturer.</i></p>"
        "<p><b>Qt</b><br>"
        "Runtime: %3<br>"
        "Built against: %4<br>"
        "Typically used under LGPL v3 (open-source Qt) as shared libraries — "
        "see third-party notices.</p>"
        "<p><b>License</b><br>"
        "This application: <b>MIT</b> — "
        "<a href=\"%5\">LICENSE</a><br>"
        "Third-party (Qt, libusb, QtRadiacode, …): "
        "<a href=\"%6\">THIRD_PARTY.md</a></p>"
        "<p><b>Source &amp; releases</b><br>"
        "<a href=\"%7\">%7</a><br>"
        "<a href=\"%8\">%8</a></p>")
                             .arg(appVersion.isEmpty() ? QStringLiteral("—") : appVersion,
                                  QString::fromUtf8(kQtRadiacodeUrl), qtRuntime, qtBuild,
                                  QString::fromUtf8(kLicenseUrl),
                                  QString::fromUtf8(kThirdPartyUrl),
                                  QString::fromUtf8(kRepoUrl),
                                  QString::fromUtf8(kReleasesUrl));

    QMessageBox box(this);
    box.setWindowTitle(tr("About Radiacode Monitor"));
    box.setTextFormat(Qt::RichText);
    box.setText(text);
    box.setIconPixmap(windowIcon().pixmap(64, 64));
    box.setStandardButtons(QMessageBox::Ok);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.exec();
}

void MainWindow::onAboutQt()
{
    QMessageBox::aboutQt(this, tr("About Qt"));
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
    if (m_waterfall) {
        m_waterfall->clear();
        // Keep BG on waterfall if loaded (history restarts empty after reset).
        if (m_hasBackground) {
            m_waterfall->setBackground(m_backgroundSpectrum.counts,
                                       m_backgroundSpectrum.durationSec);
        }
    }
    m_hasSpectrum = false;
    m_lastSpectrum = {};
    m_spectrumLiveLabel->setText(QStringLiteral("—"));
    m_spectrumTotalLabel->setText(QStringLiteral("—"));
    m_saveSpectrumBtn->setEnabled(false);
    if (m_saveSpectrumAct) {
        m_saveSpectrumAct->setEnabled(false);
    }
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
    const QtRadiacode::RcSpectrum sp = spectrumForView();
    if (sp.counts.isEmpty()) {
        appendLog(tr("No spectrum to save yet."));
        return;
    }

    const auto view = static_cast<SpectrumView>(
        m_spectrumViewCombo ? m_spectrumViewCombo->currentData().toInt()
                            : int(SpectrumView::Live));
    QString viewTag = QStringLiteral("live");
    if (view == SpectrumView::Background) {
        viewTag = QStringLiteral("bg");
    } else if (view == SpectrumView::Net) {
        viewTag = QStringLiteral("net");
    }

    const QString serial = m_device->serialNumber().isEmpty()
        ? QStringLiteral("unknown")
        : m_device->serialNumber();
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    // Basename without extension — suffix is applied from the selected format.
    const QString baseName = QStringLiteral("%1_%2_%3s_%4")
                                 .arg(serial, viewTag)
                                 .arg(sp.durationSec)
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
        sp,
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

    appendLog(tr("Spectrum saved as %1 (%2, %3 s live, %4 ch) → %5")
                  .arg(fmtName, viewTag)
                  .arg(sp.durationSec)
                  .arg(sp.counts.size())
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
    m_serialLabel->setText(m_device->serialNumber().isEmpty()
                               ? QStringLiteral("—")
                               : m_device->serialNumber());
    m_fwLabel->setText(m_device->firmwareVersion().isEmpty()
                           ? QStringLiteral("—")
                           : m_device->firmwareVersion());
    if (m_waterfall) {
        m_waterfall->setDeviceSerial(m_device->serialNumber());
    }
    syncSpectrogramRecording();
    m_statusLabel->setText(tr("Connected"));
    m_statusLabel->setToolTip(
        tr("Serial: %1\nFirmware: %2")
            .arg(m_device->serialNumber(), m_device->firmwareVersion()));
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
    abortAcquisitionIfActive(tr("disconnected"));
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
    m_statusLabel->setToolTip(QString());
    m_doseLabel->setText(QStringLiteral("—"));
    m_countLabel->setText(QStringLiteral("—"));
    m_tempLabel->setText(QStringLiteral("—"));
    m_batteryLabel->setText(QStringLiteral("—"));
    m_signalLabel->setText(QStringLiteral("—"));
    m_spectrumLiveLabel->setText(QStringLiteral("—"));
    m_spectrumTotalLabel->setText(QStringLiteral("—"));
    m_serialLabel->setText(QStringLiteral("—"));
    m_fwLabel->setText(QStringLiteral("—"));
    m_spectrum->clear();
    if (m_spectrogramRecorder) {
        m_spectrogramRecorder->stop();
    }
    if (m_waterfall) {
        m_waterfall->setDeviceSerial(QString());
        m_waterfall->clear();
    }
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
    if (m_roiPanel) {
        m_roiPanel->onSpectrum(sp);
    }
    if (m_acquisition) {
        m_acquisition->onSpectrum(sp);
    }
    // Waterfall stores live snapshots; display follows Live/Net view (Net uses BG).
    if (m_waterfall) {
        m_waterfall->setCalibration(sp.a0, sp.a1, sp.a2);
        m_waterfall->pushSpectrum(sp.counts, sp.durationSec);
    }
    // Start continuous file recording once channels are known (if checkbox on).
    if (m_recordSpectrogramCheck && m_recordSpectrogramCheck->isChecked()
        && m_spectrogramRecorder && !m_spectrogramRecorder->isRecording()) {
        syncSpectrogramRecording();
    }
    refreshSpectrumDisplay();
    updateBackgroundUi();

    // Prefer acquisition progress in the status bar while a run is active.
    if (!m_acquisition || !m_acquisition->isActive()) {
        const quint64 total = spectrumTotalCounts(sp);
        statusBar()->showMessage(
            tr("Spectrum: total %1 counts | %2 channels | live time %3")
                .arg(total)
                .arg(sp.counts.size())
                .arg(formatDuration(sp.durationSec)),
            3000);
    }
}

bool MainWindow::loadBackgroundFromFile(const QString &path)
{
    QtRadiacode::RcSpectrum sp;
    const QString err = SpectrumExport::readSpectrumFile(path, &sp);
    if (!err.isEmpty()) {
        appendLog(tr("Load BG failed: %1").arg(err));
        QMessageBox::warning(this, tr("Load background"), err);
        return false;
    }
    if (sp.counts.isEmpty()) {
        appendLog(tr("Load BG failed: spectrum has no channels."));
        return false;
    }
    m_backgroundSpectrum = sp;
    m_hasBackground = true;
    if (m_waterfall) {
        m_waterfall->setBackground(sp.counts, sp.durationSec);
    }
    const quint64 n = spectrumTotalCounts(m_backgroundSpectrum);
    appendLog(tr("Background loaded from %1 — live time %2, total counts %3, %4 ch")
                  .arg(QFileInfo(path).fileName())
                  .arg(formatDuration(m_backgroundSpectrum.durationSec))
                  .arg(n)
                  .arg(m_backgroundSpectrum.counts.size()));
    updateBackgroundUi();
    refreshSpectrumDisplay();
    // If already on Net, rebuild waterfall in net mode with new BG.
    onSpectrumViewChanged();
    return true;
}

void MainWindow::onLoadBackground()
{
    QSettings settings;
    const QString lastDir = settings
                                .value(QStringLiteral("spectrumExport/dir"),
                                       QStandardPaths::writableLocation(
                                           QStandardPaths::DocumentsLocation))
                                .toString();
    const QString startDir =
        QDir(lastDir).exists()
            ? lastDir
            : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Load background spectrum"),
        startDir,
        SpectrumExport::openFormatFilterString());
    if (path.isEmpty()) {
        return;
    }

    settings.setValue(QStringLiteral("spectrumExport/dir"),
                      QFileInfo(path).absolutePath());
    loadBackgroundFromFile(path);
}

void MainWindow::onSpectrumViewChanged()
{
    // Waterfall follows Live vs Net; Background view keeps last waterfall mode (usually Live).
    if (m_waterfall && m_spectrumViewCombo) {
        const auto view =
            static_cast<SpectrumView>(m_spectrumViewCombo->currentData().toInt());
        if (view == SpectrumView::Net) {
            m_waterfall->setDisplayMode(SpectrumWaterfall::DisplayMode::Net);
        } else {
            // Live or Background → show Live rates in waterfall
            m_waterfall->setDisplayMode(SpectrumWaterfall::DisplayMode::Live);
        }
    }
    refreshSpectrumDisplay();
    updateBackgroundUi();
}

quint64 MainWindow::spectrumTotalCounts(const QtRadiacode::RcSpectrum &sp)
{
    quint64 total = 0;
    for (quint32 c : sp.counts) {
        total += c;
    }
    return total;
}

QtRadiacode::RcSpectrum MainWindow::computeNetSpectrum(const QtRadiacode::RcSpectrum &sample,
                                                       const QtRadiacode::RcSpectrum &background)
{
    QtRadiacode::RcSpectrum net;
    net.durationSec = sample.durationSec;
    net.a0 = sample.a0;
    net.a1 = sample.a1;
    net.a2 = sample.a2;

    const int n = qMin(sample.counts.size(), background.counts.size());
    if (n <= 0) {
        return net;
    }

    // Scale BG to sample live time so unequal run lengths stay comparable.
    const double scale = (background.durationSec > 0)
        ? double(sample.durationSec) / double(background.durationSec)
        : 1.0;

    net.counts.resize(sample.counts.size());
    for (int i = 0; i < sample.counts.size(); ++i) {
        if (i >= n) {
            net.counts[i] = sample.counts[i];
            continue;
        }
        const double bgScaled = double(background.counts[i]) * scale;
        const double v = double(sample.counts[i]) - bgScaled;
        net.counts[i] = v > 0.0 ? static_cast<quint32>(v + 0.5) : 0u;
    }
    return net;
}

QtRadiacode::RcSpectrum MainWindow::spectrumForView() const
{
    const auto view = m_spectrumViewCombo
        ? static_cast<SpectrumView>(m_spectrumViewCombo->currentData().toInt())
        : SpectrumView::Live;

    switch (view) {
    case SpectrumView::Background:
        return m_hasBackground ? m_backgroundSpectrum : QtRadiacode::RcSpectrum{};
    case SpectrumView::Net:
        if (m_hasBackground && m_hasSpectrum) {
            return computeNetSpectrum(m_lastSpectrum, m_backgroundSpectrum);
        }
        return m_hasSpectrum ? m_lastSpectrum : QtRadiacode::RcSpectrum{};
    case SpectrumView::Live:
    default:
        return m_hasSpectrum ? m_lastSpectrum : QtRadiacode::RcSpectrum{};
    }
}

void MainWindow::refreshSpectrumDisplay()
{
    const QtRadiacode::RcSpectrum sp = spectrumForView();
    if (sp.counts.isEmpty()) {
        m_spectrum->clear();
        m_spectrumLiveLabel->setText(QStringLiteral("—"));
        m_spectrumTotalLabel->setText(QStringLiteral("—"));
    } else {
        m_spectrum->setSpectrum(sp.counts, sp.a0, sp.a1, sp.a2);
        m_spectrumLiveLabel->setText(formatDuration(sp.durationSec));
        m_spectrumTotalLabel->setText(tr("%1").arg(spectrumTotalCounts(sp)));
    }

    const bool canSave = !sp.counts.isEmpty();
    m_saveSpectrumBtn->setEnabled(canSave);
    if (m_saveSpectrumAct) {
        m_saveSpectrumAct->setEnabled(canSave);
    }
}

void MainWindow::updateBackgroundUi()
{
    if (m_loadBgBtn) {
        m_loadBgBtn->setEnabled(true);
    }
    // View modes (Background / Net) only make sense after a BG is loaded.
    if (m_spectrumViewCombo) {
        m_spectrumViewCombo->setEnabled(m_hasBackground);
        if (!m_hasBackground && m_spectrumViewCombo->currentIndex() != 0) {
            // Force Live without treating it as a user selection loop.
            const QSignalBlocker blocker(m_spectrumViewCombo);
            m_spectrumViewCombo->setCurrentIndex(0);
        }
    }
    if (m_bgStatusLabel) {
        if (!m_hasBackground) {
            m_bgStatusLabel->setText(tr("BG: none — Load BG… to enable view modes"));
        } else {
            m_bgStatusLabel->setText(
                tr("BG: live time %1 · total counts %2 · %3 ch")
                    .arg(formatDuration(m_backgroundSpectrum.durationSec))
                    .arg(spectrumTotalCounts(m_backgroundSpectrum))
                    .arg(m_backgroundSpectrum.counts.size()));
        }
    }
}

void MainWindow::onAcquisitionStart()
{
    if (!m_acquisition || !m_device) {
        return;
    }
    if (m_device->state() != QtRadiacode::RadiaCodeDevice::State::Connected) {
        appendLog(tr("Acquisition: connect a device first."));
        return;
    }
    if (m_acquisition->isActive()) {
        return;
    }

    const auto mode = static_cast<AcquisitionController::Mode>(
        m_acqModeCombo->currentData().toInt());
    const quint64 target = static_cast<quint64>(m_acqTargetSpin->value());

    // If spectrum already has data, choose: continue accumulating, reset first, or save.
    bool reset = false;
    while (true) {
        quint64 existingCounts = 0;
        for (quint32 c : m_lastSpectrum.counts) {
            existingCounts += c;
        }
        const quint32 liveSec = m_lastSpectrum.durationSec;
        const bool hasData = m_hasSpectrum && (existingCounts > 0 || liveSec > 0);
        if (!hasData) {
            // Empty spectrum — no need to ask; start from current (already ~zero).
            reset = false;
            break;
        }

        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Start acquisition"));
        box.setText(
            tr("Current spectrum already has data.\n\n"
               "Live time %1 · total counts %2")
                .arg(formatDuration(liveSec))
                .arg(existingCounts));
        box.setInformativeText(
            tr("Continue keeps the accumulation and runs until the target.\n"
               "Reset and start clears the spectrum on the device first.\n"
               "Save… stores the current spectrum without starting yet."));

        auto *continueBtn = box.addButton(tr("Continue…"), QMessageBox::AcceptRole);
        auto *saveBtn = box.addButton(tr("Save…"), QMessageBox::ActionRole);
        auto *resetBtn =
            box.addButton(tr("Reset and start"), QMessageBox::DestructiveRole);
        auto *cancelBtn = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(continueBtn);
        box.exec();

        QAbstractButton *clicked = box.clickedButton();
        if (clicked == cancelBtn || clicked == nullptr) {
            return;
        }
        if (clicked == saveBtn) {
            onSaveSpectrum();
            continue; // ask again after save
        }
        if (clicked == continueBtn) {
            reset = false;
            break;
        }
        if (clicked == resetBtn) {
            reset = true;
            break;
        }
        return;
    }

    if (m_acqProgressBar) {
        m_acqProgressBar->setValue(0);
    }
    m_acquisition->start(mode, target, reset);
    updateAcquisitionUi();
}

void MainWindow::onAcquisitionStop()
{
    abortAcquisitionIfActive(tr("stopped by user"));
}

void MainWindow::onAcquisitionModeChanged()
{
    if (!m_acqModeCombo || !m_acqTargetSpin) {
        return;
    }
    const auto mode = static_cast<AcquisitionController::Mode>(
        m_acqModeCombo->currentData().toInt());
    if (mode == AcquisitionController::Mode::TimeSeconds) {
        m_acqTargetSpin->setSuffix(tr(" s"));
        m_acqTargetSpin->setSingleStep(10);
        // Keep a sensible default when switching from large count targets.
        if (m_acqTargetSpin->value() > 86400) {
            m_acqTargetSpin->setValue(300);
        }
        m_acqTargetSpin->setMaximum(86400);
    } else {
        m_acqTargetSpin->setSuffix(QString());
        m_acqTargetSpin->setSingleStep(1000);
        m_acqTargetSpin->setMaximum(2000000000);
        if (m_acqTargetSpin->value() < 1000) {
            m_acqTargetSpin->setValue(100000);
        }
    }
}

void MainWindow::updateAcquisitionUi()
{
    const bool connected =
        m_device && m_device->state() == QtRadiacode::RadiaCodeDevice::State::Connected;
    const bool active = m_acquisition && m_acquisition->isActive();

    if (m_acqStartBtn) {
        m_acqStartBtn->setEnabled(connected && !active);
    }
    if (m_acqStopBtn) {
        m_acqStopBtn->setEnabled(active);
    }
    if (m_acqModeCombo) {
        m_acqModeCombo->setEnabled(connected && !active);
    }
    if (m_acqTargetSpin) {
        m_acqTargetSpin->setEnabled(connected && !active);
    }
    if (!active) {
        if (m_acqProgressLabel
            && (m_acqProgressLabel->text().isEmpty()
                || m_acqProgressLabel->text() == QLatin1String("—"))) {
            m_acqProgressLabel->setText(tr("—"));
        }
        // Keep final 100% after complete; only clear bar when fully idle with no text.
        if (m_acqProgressBar && m_acqProgressLabel
            && m_acqProgressLabel->text() == QLatin1String("—")) {
            m_acqProgressBar->setValue(0);
        }
    }
}

void MainWindow::abortAcquisitionIfActive(const QString &reason)
{
    if (m_acquisition && m_acquisition->isActive()) {
        m_acquisition->abort(reason);
    }
    if (m_acqProgressBar) {
        m_acqProgressBar->setValue(0);
    }
    if (m_acqProgressLabel) {
        m_acqProgressLabel->setText(tr("—"));
    }
    updateAcquisitionUi();
}

void MainWindow::setConnectedUi(bool connected)
{
    m_connectBtn->setEnabled(!connected);
    m_disconnectBtn->setEnabled(connected);
    m_resetSpectrumBtn->setEnabled(connected);
    m_deviceCombo->setEnabled(!connected);
    updateRefreshButton();
    if (m_roiPanel) {
        m_roiPanel->setConnected(connected);
    }
    updateAcquisitionUi();
    updateBackgroundUi();
    refreshSpectrumDisplay(); // keep Save enabled offline when spectrum cached
}

void MainWindow::appendLog(const QString &line)
{
    statusBar()->showMessage(line, 15000);
}
