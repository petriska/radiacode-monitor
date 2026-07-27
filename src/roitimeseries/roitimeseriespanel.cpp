#include "roitimeseries/roitimeseriespanel.h"

#include "roitimeseries/roiexport.h"
#include "roitimeseries/timeserieswidget.h"

#include "device/radiacodedevice.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
enum Col { ColEnabled = 0, ColName, ColEMin, ColEMax, ColCount };
}

RoiTimeSeriesPanel::RoiTimeSeriesPanel(QtRadiacode::RadiaCodeDevice *device, QWidget *parent)
    : QWidget(parent)
    , m_device(device)
{
    m_recorder = new RoiTimeSeriesRecorder(this);
    m_recorder->setRois(presetRadonDaughters());

    auto *root = new QVBoxLayout(this);

    auto *ctrl = new QGroupBox(tr("ROI time series"), this);
    auto *ctrlLay = new QVBoxLayout(ctrl);

    auto *rowPreset = new QHBoxLayout;
    rowPreset->addWidget(new QLabel(tr("Preset:"), this));
    m_presetCombo = new QComboBox(this);
    for (const RoiPreset &p : roiPresets()) {
        m_presetCombo->addItem(p.name, p.id);
    }
    rowPreset->addWidget(m_presetCombo, 1);
    ctrlLay->addLayout(rowPreset);

    m_roiTable = new QTableWidget(0, ColCount, this);
    m_roiTable->setHorizontalHeaderLabels(
        {tr("On"), tr("Name"), tr("E min (keV)"), tr("E max (keV)")});
    m_roiTable->horizontalHeader()->setStretchLastSection(true);
    m_roiTable->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_roiTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_roiTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_roiTable->setMaximumHeight(140);
    ctrlLay->addWidget(m_roiTable);

    auto *rowRoiBtn = new QHBoxLayout;
    m_addRoiBtn = new QPushButton(tr("Add ROI"), this);
    m_removeRoiBtn = new QPushButton(tr("Remove"), this);
    rowRoiBtn->addWidget(m_addRoiBtn);
    rowRoiBtn->addWidget(m_removeRoiBtn);
    rowRoiBtn->addStretch(1);
    ctrlLay->addLayout(rowRoiBtn);

    auto *row1 = new QHBoxLayout;
    m_startBtn = new QPushButton(tr("Start recording"), this);
    m_stopBtn = new QPushButton(tr("Stop"), this);
    m_clearBtn = new QPushButton(tr("Clear"), this);
    m_t0Btn = new QPushButton(tr("Set t₀ marker"), this);
    m_t0Btn->setToolTip(
        tr("Mark experiment start: radon flush, end of irradiation, etc.\n"
           "CSV elapsed_s is measured from this time."));
    m_exportBtn = new QPushButton(tr("Export CSV…"), this);
    m_stopBtn->setEnabled(false);
    row1->addWidget(m_startBtn);
    row1->addWidget(m_stopBtn);
    row1->addWidget(m_clearBtn);
    row1->addWidget(m_t0Btn);
    row1->addWidget(m_exportBtn);
    row1->addStretch(1);
    ctrlLay->addLayout(row1);

    auto *row2 = new QHBoxLayout;
    m_resetOnStart = new QCheckBox(tr("Reset spectrum on start"), this);
    m_resetOnStart->setChecked(true);
    m_resetOnStart->setToolTip(
        tr("Clears device spectrum accumulation when recording starts "
           "(recommended for clean live times)."));
    m_dwellCombo = new QComboBox(this);
    for (int s : {5, 10, 15, 20, 30, 60}) {
        m_dwellCombo->addItem(tr("%1 s").arg(s), s);
    }
    m_dwellCombo->setCurrentIndex(2);
    row2->addWidget(m_resetOnStart);
    row2->addWidget(new QLabel(tr("Dwell:"), this));
    row2->addWidget(m_dwellCombo);
    row2->addStretch(1);
    ctrlLay->addLayout(row2);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    ctrlLay->addWidget(m_statusLabel);

    root->addWidget(ctrl);

    m_chart = new TimeSeriesWidget(this);
    root->addWidget(m_chart, 1);

    loadRoisToTable(presetRadonDaughters());

    connect(m_startBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onStart);
    connect(m_stopBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onStop);
    connect(m_clearBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onClear);
    connect(m_t0Btn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onSetT0);
    connect(m_exportBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onExport);
    connect(m_addRoiBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onAddRoi);
    connect(m_removeRoiBtn, &QPushButton::clicked, this, &RoiTimeSeriesPanel::onRemoveRoi);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RoiTimeSeriesPanel::onPresetChanged);
    connect(m_roiTable, &QTableWidget::itemChanged, this, &RoiTimeSeriesPanel::onTableChanged);

    connect(m_recorder, &RoiTimeSeriesRecorder::sampleAdded, this, [this](const RoiTimeSample &) {
        m_chart->setSamples(m_recorder->samples(), m_recorder->hasT0(), m_recorder->t0());
        updateStatus();
    });
    connect(m_recorder, &RoiTimeSeriesRecorder::samplesCleared, this, [this] {
        m_chart->clear();
        updateStatus();
    });

    updateStatus();
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
        m_roiTable->setItem(row, ColName, new QTableWidgetItem(r.name));
        m_roiTable->setItem(row, ColEMin, new QTableWidgetItem(QString::number(r.eMinKeV, 'f', 1)));
        m_roiTable->setItem(row, ColEMax, new QTableWidgetItem(QString::number(r.eMaxKeV, 'f', 1)));
        // Keep stable id in UserRole on name cell.
        m_roiTable->item(row, ColName)->setData(Qt::UserRole, r.id);
    }
    m_blockTableSignal = false;
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
    name->setData(Qt::UserRole, makeRoiId(name->text()));
    m_roiTable->setItem(row, ColName, name);
    m_roiTable->setItem(row, ColEMin, new QTableWidgetItem(QStringLiteral("100")));
    m_roiTable->setItem(row, ColEMax, new QTableWidgetItem(QStringLiteral("200")));
    m_blockTableSignal = false;
    m_roiTable->selectRow(row);
}

void RoiTimeSeriesPanel::onRemoveRoi()
{
    if (m_recording) {
        return;
    }
    const int row = m_roiTable->currentRow();
    if (row >= 0) {
        m_roiTable->removeRow(row);
    }
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
    m_chart->setSamples(m_recorder->samples(), m_recorder->hasT0(), m_recorder->t0());
    emit logMessage(tr("ROI time series: t₀ set to %1 (UTC).")
                        .arg(t0.toString(Qt::ISODateWithMs)));
    updateStatus();
}

void RoiTimeSeriesPanel::onExport()
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
