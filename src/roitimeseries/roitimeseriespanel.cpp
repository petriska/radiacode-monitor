#include "roitimeseries/roitimeseriespanel.h"

#include "roitimeseries/roiexport.h"
#include "roitimeseries/timeserieswidget.h"

#include "device/radiacodedevice.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {
enum Col {
    ColEnabled = 0,
    ColName,
    ColEMin,
    ColEMax,
    ColCounts, // cumulative counts in ROI from spectrum start
    ColCps,    // ΔN/Δt over last spectrum interval (chart value while recording)
    ColCount
};

QTableWidgetItem *makeReadOnlyStatsItem()
{
    auto *item = new QTableWidgetItem(QStringLiteral("—"));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}
}

RoiTimeSeriesPanel::RoiTimeSeriesPanel(QtRadiacode::RadiaCodeDevice *device, QWidget *parent)
    : QWidget(parent)
    , m_device(device)
{
    m_recorder = new RoiTimeSeriesRecorder(this);

    // Controls live in MainWindow setup ROI tab; this panel is chart-only.
    m_controlsBox = new QWidget(this);
    m_controlsBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *ctrlLay = new QVBoxLayout(m_controlsBox);
    ctrlLay->setContentsMargins(6, 6, 6, 6);
    ctrlLay->setSpacing(6);

    auto *rowPreset = new QHBoxLayout;
    rowPreset->addWidget(new QLabel(tr("Preset:"), m_controlsBox));
    m_presetCombo = new QComboBox(m_controlsBox);
    for (const RoiPreset &p : roiPresets()) {
        m_presetCombo->addItem(p.name, p.id);
    }
    rowPreset->addWidget(m_presetCombo, 1);
    ctrlLay->addLayout(rowPreset);

    m_roiTable = new QTableWidget(0, ColCount, m_controlsBox);
    m_roiTable->setHorizontalHeaderLabels({tr("On"), tr("Name"), tr("E min (keV)"),
                                           tr("E max (keV)"), tr("Counts"), tr("cps")});
    if (auto *hCounts = m_roiTable->horizontalHeaderItem(ColCounts)) {
        hCounts->setToolTip(
            tr("Total counts in this energy window from the current spectrum\n"
               "(accumulated since the spectrum was started / last reset)."));
    }
    if (auto *hCps = m_roiTable->horizontalHeaderItem(ColCps)) {
        hCps->setToolTip(
            tr("Count rate in this window over the last spectrum interval (ΔN/Δt).\n"
               "While recording, this is the same value written to the ROI time series chart."));
    }
    m_roiTable->horizontalHeader()->setStretchLastSection(false);
    m_roiTable->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_roiTable->horizontalHeader()->setSectionResizeMode(ColCounts, QHeaderView::ResizeToContents);
    m_roiTable->horizontalHeader()->setSectionResizeMode(ColCps, QHeaderView::ResizeToContents);
    m_roiTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_roiTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_roiTable->setMinimumHeight(100);
    // No max height: table absorbs extra vertical space when stretched to Live height.
    m_roiTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Click outside the table (or on empty space) clears the highlighted row.
    m_roiTable->viewport()->installEventFilter(this);
    qApp->installEventFilter(this);
    ctrlLay->addWidget(m_roiTable, 1);

    auto *rowRoiBtn = new QHBoxLayout;
    m_addRoiBtn = new QPushButton(tr("Add ROI"), m_controlsBox);
    m_removeRoiBtn = new QPushButton(tr("Remove"), m_controlsBox);
    rowRoiBtn->addWidget(m_addRoiBtn);
    rowRoiBtn->addWidget(m_removeRoiBtn);
    rowRoiBtn->addStretch(1);
    ctrlLay->addLayout(rowRoiBtn);

    // Stack action buttons so the ROI setup tab can stay narrow.
    m_startBtn = new QPushButton(tr("Start recording"), m_controlsBox);
    m_stopBtn = new QPushButton(tr("Stop"), m_controlsBox);
    m_clearBtn = new QPushButton(tr("Clear"), m_controlsBox);
    m_t0Btn = new QPushButton(tr("Set t₀ marker"), m_controlsBox);
    m_t0Btn->setToolTip(
        tr("Mark experiment start: radon flush, end of irradiation, etc.\n"
           "CSV elapsed_s is measured from this time."));
    m_exportBtn = new QPushButton(tr("Export CSV…"), m_controlsBox);
    m_stopBtn->setEnabled(false);
    ctrlLay->addWidget(m_startBtn);
    ctrlLay->addWidget(m_stopBtn);
    ctrlLay->addWidget(m_clearBtn);
    ctrlLay->addWidget(m_t0Btn);
    ctrlLay->addWidget(m_exportBtn);

    m_resetOnStart = new QCheckBox(tr("Reset spectrum on start"), m_controlsBox);
    m_resetOnStart->setChecked(true);
    m_resetOnStart->setToolTip(
        tr("Clears device spectrum accumulation when recording starts "
           "(recommended for clean live times)."));
    m_dwellCombo = new QComboBox(m_controlsBox);
    for (int s : {5, 10, 15, 20, 30, 60}) {
        m_dwellCombo->addItem(tr("%1 s").arg(s), s);
    }
    m_dwellCombo->setCurrentIndex(2);
    ctrlLay->addWidget(m_resetOnStart);
    auto *dwellRow = new QHBoxLayout;
    dwellRow->addWidget(new QLabel(tr("Dwell:"), m_controlsBox));
    dwellRow->addWidget(m_dwellCombo, 1);
    ctrlLay->addLayout(dwellRow);

    m_statusLabel = new QLabel(m_controlsBox);
    m_statusLabel->setWordWrap(true);
    ctrlLay->addWidget(m_statusLabel);

    // Tab body: chart only (controls reparented by MainWindow).
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_chart = new TimeSeriesWidget(this);
    root->addWidget(m_chart, 1);

    connect(m_startBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onStart);
    connect(m_stopBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onStop);
    connect(m_clearBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onClear);
    connect(m_t0Btn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onSetT0);
    connect(m_exportBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::exportCsv);
    connect(m_addRoiBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onAddRoi);
    connect(m_removeRoiBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onRemoveRoi);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RoiTimeSeriesPanel::onPresetChanged);
    connect(m_roiTable, &QTableWidget::itemChanged, this, &RoiTimeSeriesPanel::onTableChanged);
    connect(m_dwellCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { saveSettings(); });
    connect(m_resetOnStart, &QCheckBox::toggled, this, [this](bool) { saveSettings(); });

    connect(m_recorder, &RoiTimeSeriesRecorder::sampleAdded, this, [this](const RoiTimeSample &) {
        m_chart->setSamples(m_recorder->samples(), m_recorder->hasT0(), m_recorder->t0(),
                            m_recorder->rois());
        updateStatus();
    });
    connect(m_recorder, &RoiTimeSeriesRecorder::samplesCleared, this, [this] {
        m_chart->clear();
        updateStatus();
    });

    loadSettings();
    m_recorder->setRois(roisFromTable());
    // Defer so MainWindow can connect to roisChanged first.
    QTimer::singleShot(0, this, [this] { emitRoisChanged(); });

    updateStatus();
}

RoiTimeSeriesPanel::~RoiTimeSeriesPanel()
{
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

void RoiTimeSeriesPanel::loadSettings()
{
    QSettings settings;
    const QString presetId = settings
                                 .value(QStringLiteral("roiTimeSeries/presetId"),
                                        QStringLiteral("radon_daughters"))
                                 .toString();
    const int dwellSec =
        settings.value(QStringLiteral("roiTimeSeries/dwellSeconds"), 15).toInt();
    const bool resetOnStart =
        settings.value(QStringLiteral("roiTimeSeries/resetSpectrumOnStart"), true).toBool();

    {
        QSignalBlocker blockPreset(m_presetCombo);
        int idx = m_presetCombo->findData(presetId);
        if (idx < 0) {
            idx = 0;
        }
        m_presetCombo->setCurrentIndex(idx);
    }

    {
        QSignalBlocker blockDwell(m_dwellCombo);
        int dwellIdx = m_dwellCombo->findData(dwellSec);
        if (dwellIdx < 0) {
            dwellIdx = m_dwellCombo->findData(15);
        }
        if (dwellIdx < 0) {
            dwellIdx = 0;
        }
        m_dwellCombo->setCurrentIndex(dwellIdx);
    }

    {
        QSignalBlocker blockReset(m_resetOnStart);
        m_resetOnStart->setChecked(resetOnStart);
    }

    const QString id = m_presetCombo->currentData().toString();
    loadRoisToTable(presetById(id).rois);
}

void RoiTimeSeriesPanel::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("roiTimeSeries/presetId"),
                      m_presetCombo->currentData().toString());
    settings.setValue(QStringLiteral("roiTimeSeries/dwellSeconds"), dwellSeconds());
    settings.setValue(QStringLiteral("roiTimeSeries/resetSpectrumOnStart"),
                      m_resetOnStart->isChecked());
}

void RoiTimeSeriesPanel::loadRoisToTable(const QVector<RoiWindow> &rois)
{
    m_blockTableSignal = true;
    m_roiTable->setRowCount(0);
    for (const RoiWindow &r : rois) {
        const int row = m_roiTable->rowCount();
        m_roiTable->insertRow(row);
        auto *en = new QTableWidgetItem;
        en->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        en->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        m_roiTable->setItem(row, ColEnabled, en);
        auto *nameItem = new QTableWidgetItem(r.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        nameItem->setData(Qt::UserRole, r.id); // stable id for CSV / chart series
        m_roiTable->setItem(row, ColName, nameItem);
        auto *eMinItem = new QTableWidgetItem(QString::number(r.eMinKeV, 'f', 1));
        eMinItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        m_roiTable->setItem(row, ColEMin, eMinItem);
        auto *eMaxItem = new QTableWidgetItem(QString::number(r.eMaxKeV, 'f', 1));
        eMaxItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        m_roiTable->setItem(row, ColEMax, eMaxItem);
        ensureStatsItems(row);
    }
    m_blockTableSignal = false;
    resetRoiRateBaseline();
    if (m_hasLastSpectrum) {
        updateRoiLiveStats(m_lastSpectrum);
    }
    emitRoisChanged();
}

void RoiTimeSeriesPanel::ensureStatsItems(int row)
{
    if (!m_roiTable->item(row, ColCounts)) {
        m_roiTable->setItem(row, ColCounts, makeReadOnlyStatsItem());
    }
    if (!m_roiTable->item(row, ColCps)) {
        m_roiTable->setItem(row, ColCps, makeReadOnlyStatsItem());
    }
}

void RoiTimeSeriesPanel::resetRoiRateBaseline()
{
    m_hasPrevStats = false;
    m_prevLiveSec = 0;
    m_prevRoiCounts.clear();
}

void RoiTimeSeriesPanel::clearRoiLiveStats()
{
    m_hasLastSpectrum = false;
    m_lastSpectrum = {};
    resetRoiRateBaseline();
    m_blockTableSignal = true;
    for (int row = 0; row < m_roiTable->rowCount(); ++row) {
        ensureStatsItems(row);
        m_roiTable->item(row, ColCounts)->setText(QStringLiteral("—"));
        m_roiTable->item(row, ColCps)->setText(QStringLiteral("—"));
    }
    m_blockTableSignal = false;
}

void RoiTimeSeriesPanel::updateRoiLiveStats(const QtRadiacode::RcSpectrum &sp)
{
    if (sp.counts.isEmpty()) {
        return;
    }

    m_lastSpectrum = sp;
    m_hasLastSpectrum = true;

    // Spectrum was reset on device (live time went backwards).
    if (m_hasPrevStats && sp.durationSec < m_prevLiveSec) {
        resetRoiRateBaseline();
    }

    const int n = sp.counts.size();
    const QVector<RoiWindow> rois = roisFromTable();
    QVector<quint64> curCounts(rois.size(), 0);
    QVector<bool> valid(rois.size(), false);

    for (int i = 0; i < rois.size(); ++i) {
        int c0 = 0;
        int c1 = 0;
        if (roiToChannels(rois[i], sp.a0, sp.a1, sp.a2, n, &c0, &c1)) {
            curCounts[i] = sumChannels(sp.counts, c0, c1);
            valid[i] = true;
        }
    }

    const bool canDelta = m_hasPrevStats && sp.durationSec > m_prevLiveSec
        && m_prevRoiCounts.size() == curCounts.size();
    const bool sameLive = m_hasPrevStats && sp.durationSec == m_prevLiveSec
        && m_prevRoiCounts.size() == curCounts.size();
    const double dt = canDelta ? static_cast<double>(sp.durationSec - m_prevLiveSec) : 0.0;

    m_blockTableSignal = true;
    for (int row = 0; row < m_roiTable->rowCount() && row < rois.size(); ++row) {
        ensureStatsItems(row);
        if (!valid[row]) {
            m_roiTable->item(row, ColCounts)->setText(QStringLiteral("—"));
            m_roiTable->item(row, ColCps)->setText(QStringLiteral("—"));
            continue;
        }
        m_roiTable->item(row, ColCounts)->setText(QString::number(curCounts[row]));
        if (canDelta && dt > 0.0) {
            // Same formula as RoiTimeSeriesRecorder → chart series.
            const quint64 dN = (curCounts[row] >= m_prevRoiCounts[row])
                ? (curCounts[row] - m_prevRoiCounts[row])
                : 0;
            const double cps = static_cast<double>(dN) / dt;
            m_roiTable->item(row, ColCps)->setText(QString::number(cps, 'f', 2));
        } else if (sameLive) {
            // Live time unchanged: keep previous cps text (do not flash "—").
        } else if (sp.durationSec > 0) {
            // First sample after connect/reset/edit: average rate over full live time.
            const double cps =
                static_cast<double>(curCounts[row]) / static_cast<double>(sp.durationSec);
            m_roiTable->item(row, ColCps)->setText(QString::number(cps, 'f', 2));
        } else {
            m_roiTable->item(row, ColCps)->setText(QStringLiteral("—"));
        }
    }
    m_blockTableSignal = false;

    // Advance baseline only when live time moved forward (or after reset baseline).
    if (!sameLive) {
        m_hasPrevStats = true;
        m_prevLiveSec = sp.durationSec;
        m_prevRoiCounts = curCounts;
    }
}

void RoiTimeSeriesPanel::emitRoisChanged()
{
    emit roisChanged(roisFromTable());
}

QVector<RoiWindow> RoiTimeSeriesPanel::roisFromTable() const
{
    QVector<RoiWindow> out;
    for (int row = 0; row < m_roiTable->rowCount(); ++row) {
        RoiWindow r;
        r.enabled = m_roiTable->item(row, ColEnabled)
            && m_roiTable->item(row, ColEnabled)->checkState() == Qt::Checked;
        r.name = m_roiTable->item(row, ColName) ? m_roiTable->item(row, ColName)->text().trimmed()
                                                : QString();
        r.id = m_roiTable->item(row, ColName)
            ? m_roiTable->item(row, ColName)->data(Qt::UserRole).toString()
            : QString();
        if (r.id.isEmpty()) {
            r.id = makeRoiId(r.name);
        }
        r.eMinKeV = m_roiTable->item(row, ColEMin)
            ? m_roiTable->item(row, ColEMin)->text().toDouble()
            : 0;
        r.eMaxKeV = m_roiTable->item(row, ColEMax)
            ? m_roiTable->item(row, ColEMax)->text().toDouble()
            : 0;
        out.append(r);
    }
    return out;
}

void RoiTimeSeriesPanel::setEditingEnabled(bool on)
{
    m_presetCombo->setEnabled(on);
    m_roiTable->setEnabled(on);
    m_addRoiBtn->setEnabled(on);
    m_removeRoiBtn->setEnabled(on);
    m_dwellCombo->setEnabled(on);
}

void RoiTimeSeriesPanel::setConnected(bool connected)
{
    m_connected = connected;
    if (!connected && m_recording) {
        onStop();
    }
    if (!connected) {
        clearRoiLiveStats();
    }
    m_startBtn->setEnabled(connected && !m_recording);
    updateStatus();
}

int RoiTimeSeriesPanel::dwellSeconds() const
{
    return m_dwellCombo->currentData().toInt();
}

void RoiTimeSeriesPanel::onPresetChanged(int index)
{
    if (index < 0 || m_recording) {
        return;
    }
    const QString id = m_presetCombo->itemData(index).toString();
    const RoiPreset p = presetById(id);
    loadRoisToTable(p.rois);
    m_recorder->setRois(roisFromTable());
    saveSettings();
    emit logMessage(tr("ROI preset loaded: %1").arg(p.name));
}

void RoiTimeSeriesPanel::onAddRoi()
{
    if (m_recording) {
        return;
    }
    m_blockTableSignal = true;
    const int row = m_roiTable->rowCount();
    m_roiTable->insertRow(row);
    auto *en = new QTableWidgetItem;
    en->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    en->setCheckState(Qt::Checked);
    m_roiTable->setItem(row, ColEnabled, en);
    auto *name = new QTableWidgetItem(tr("ROI %1").arg(row + 1));
    name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    name->setData(Qt::UserRole, makeRoiId(name->text()));
    m_roiTable->setItem(row, ColName, name);
    auto *eMin = new QTableWidgetItem(QStringLiteral("100"));
    eMin->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    m_roiTable->setItem(row, ColEMin, eMin);
    auto *eMax = new QTableWidgetItem(QStringLiteral("200"));
    eMax->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    m_roiTable->setItem(row, ColEMax, eMax);
    ensureStatsItems(row);
    m_blockTableSignal = false;
    m_roiTable->selectRow(row);
    // New row invalidates previous per-row baselines (size / indices change).
    resetRoiRateBaseline();
    if (m_hasLastSpectrum) {
        updateRoiLiveStats(m_lastSpectrum);
    }
    emitRoisChanged();
}

void RoiTimeSeriesPanel::onRemoveRoi()
{
    if (m_recording || !m_roiTable) {
        return;
    }
    int row = m_roiTable->currentRow();
    if (row < 0 && m_roiTable->selectionModel()) {
        const QModelIndexList rows = m_roiTable->selectionModel()->selectedRows();
        if (!rows.isEmpty()) {
            row = rows.first().row();
        }
    }
    if (row < 0 || row >= m_roiTable->rowCount()) {
        return;
    }
    m_blockTableSignal = true;
    m_roiTable->removeRow(row);
    m_blockTableSignal = false;
    resetRoiRateBaseline();
    if (m_hasLastSpectrum) {
        updateRoiLiveStats(m_lastSpectrum);
    }
    emitRoisChanged();
    saveSettings();
}

void RoiTimeSeriesPanel::onTableChanged()
{
    if (m_blockTableSignal || m_recording) {
        return;
    }
    // Refresh id when name changes.
    for (int row = 0; row < m_roiTable->rowCount(); ++row) {
        auto *nameItem = m_roiTable->item(row, ColName);
        if (!nameItem) {
            continue;
        }
        const QString id = makeRoiId(nameItem->text());
        if (nameItem->data(Qt::UserRole).toString() != id) {
            m_blockTableSignal = true;
            nameItem->setData(Qt::UserRole, id);
            m_blockTableSignal = false;
        }
    }
    // Energy window edits: recompute Counts; reset rate baseline (windows changed).
    resetRoiRateBaseline();
    if (m_hasLastSpectrum) {
        updateRoiLiveStats(m_lastSpectrum);
    }
    emitRoisChanged();
}

void RoiTimeSeriesPanel::clearRoiTableSelection()
{
    if (!m_roiTable) {
        return;
    }
    m_roiTable->clearSelection();
    m_roiTable->setCurrentIndex(QModelIndex());
}

bool RoiTimeSeriesPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_roiTable || event->type() != QEvent::MouseButtonPress) {
        return QWidget::eventFilter(watched, event);
    }

    // Click on empty area inside the table → deselect row.
    if (watched == m_roiTable->viewport()) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (!m_roiTable->indexAt(me->pos()).isValid()) {
            clearRoiTableSelection();
        }
        return QWidget::eventFilter(watched, event);
    }

    // Click anywhere outside the table → deselect row (return to neutral look).
    // Exception: Remove needs the current row; clearing on MouseButtonPress would
    // wipe selection before the button's clicked() slot runs.
    if (auto *w = qobject_cast<QWidget *>(watched)) {
        if (m_removeRoiBtn
            && (w == m_removeRoiBtn || m_removeRoiBtn->isAncestorOf(w))) {
            return QWidget::eventFilter(watched, event);
        }
        if (w != m_roiTable && w != m_roiTable->viewport() && !m_roiTable->isAncestorOf(w)) {
            if (m_roiTable->selectionModel() && m_roiTable->selectionModel()->hasSelection()) {
                clearRoiTableSelection();
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void RoiTimeSeriesPanel::onStart()
{
    if (!m_connected || !m_device) {
        QMessageBox::warning(this, tr("ROI time series"), tr("Connect a device first."));
        return;
    }
    const QVector<RoiWindow> rois = roisFromTable();
    bool any = false;
    for (const RoiWindow &r : rois) {
        if (r.enabled) {
            if (r.eMaxKeV <= r.eMinKeV) {
                QMessageBox::warning(
                    this, tr("ROI time series"),
                    tr("ROI \"%1\": E max must be greater than E min.").arg(r.name));
                return;
            }
            any = true;
        }
    }
    if (!any) {
        QMessageBox::warning(this, tr("ROI time series"), tr("Enable at least one ROI."));
        return;
    }

    m_recorder->setRois(rois);
    m_recording = true;
    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    setEditingEnabled(false);
    emit recordingChanged(true);

    if (m_resetOnStart->isChecked()) {
        m_recorder->clear();
        emit logMessage(tr("ROI time series: recording started (dwell %1 s), resetting spectrum…")
                            .arg(dwellSeconds()));
        emit requestSpectrumReset();
    } else {
        emit logMessage(tr("ROI time series: recording started (dwell %1 s).")
                            .arg(dwellSeconds()));
        emit requestSpectrumNow();
    }
    updateStatus();
}

void RoiTimeSeriesPanel::onStop()
{
    if (!m_recording) {
        return;
    }
    m_recording = false;
    m_startBtn->setEnabled(m_connected);
    m_stopBtn->setEnabled(false);
    setEditingEnabled(true);
    emit recordingChanged(false);
    emit logMessage(tr("ROI time series: recording stopped (%1 samples).")
                        .arg(m_recorder->sampleCount()));
    updateStatus();
}

void RoiTimeSeriesPanel::onClear()
{
    if (m_recording) {
        onStop();
    }
    m_recorder->clear();
    m_recorder->clearT0();
    emit logMessage(tr("ROI time series: samples cleared."));
    updateStatus();
}

void RoiTimeSeriesPanel::onSetT0()
{
    const QDateTime t0 = QDateTime::currentDateTimeUtc();
    m_recorder->setT0(t0);
    m_chart->setSamples(m_recorder->samples(), m_recorder->hasT0(), m_recorder->t0(),
                        m_recorder->rois());
    emit logMessage(tr("ROI time series: t₀ set to %1 (UTC).")
                        .arg(t0.toString(Qt::ISODateWithMs)));
    updateStatus();
}

void RoiTimeSeriesPanel::exportCsv()
{
    if (m_recorder->sampleCount() == 0) {
        QMessageBox::information(this, tr("Export"), tr("No samples to export."));
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Export ROI time series CSV"),
        dir + QStringLiteral("/roi_timeseries.csv"),
        tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    const QString serial = m_device ? m_device->serialNumber() : QString();
    const QString err = RoiTimeSeriesExport::writeCsv(
        path,
        m_recorder->samples(),
        m_recorder->rois(),
        serial,
        m_recorder->t0(),
        m_recorder->hasT0(),
        m_recorder->lastA0(),
        m_recorder->lastA1(),
        m_recorder->lastA2());
    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("Export"), err);
        return;
    }
    emit logMessage(tr("ROI time series: exported %1 samples → %2")
                        .arg(m_recorder->sampleCount())
                        .arg(path));
}

void RoiTimeSeriesPanel::onSpectrum(const QtRadiacode::RcSpectrum &sp)
{
    // Always refresh table Counts / cps from the live spectrum.
    updateRoiLiveStats(sp);

    if (!m_recording) {
        return;
    }
    if (!hasEnergyCalibration(sp.a0, sp.a1, sp.a2)) {
        emit logMessage(
            tr("ROI time series: warning — no energy calibration; ROI rates may be zero."));
    }
    m_recorder->ingestSpectrum(sp);
}

void RoiTimeSeriesPanel::updateStatus()
{
    QString t0 = m_recorder->hasT0()
        ? m_recorder->t0().toLocalTime().toString(QStringLiteral("HH:mm:ss"))
        : tr("(not set)");
    QString last;
    const auto &samples = m_recorder->samples();
    if (!samples.isEmpty()) {
        const RoiTimeSample &s = samples.last();
        last = tr("gross %1 cps").arg(s.grossCps, 0, 'f', 2);
        for (const RoiSample &r : s.rois) {
            last += QStringLiteral(" · %1 %2").arg(r.id).arg(r.cps, 0, 'f', 2);
        }
    } else {
        last = QStringLiteral("—");
    }
    m_statusLabel->setText(
        tr("Recording: %1 | Samples: %2 | t₀: %3 | Last: %4")
            .arg(m_recording ? tr("YES") : tr("no"))
            .arg(m_recorder->sampleCount())
            .arg(t0)
            .arg(last));
}
