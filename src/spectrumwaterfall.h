#pragma once

#include <QDateTime>
#include <QImage>
#include <QVector>
#include <QWidget>

// Time × energy waterfall under the spectrum (SDR-style).
// Stores live spectrum snapshots; display mode Live or Net (Live − BG) rebuilds rate rows.
// Newest row at the bottom; older rows scroll upward.
class SpectrumWaterfall : public QWidget {
    Q_OBJECT
public:
    enum class DisplayMode { Live, Net };
    Q_ENUM(DisplayMode)

    explicit SpectrumWaterfall(QWidget *parent = nullptr);

    /// Visible channel range [xMin, xMax) — typically mirrored from SpectrumWidget zoom.
    void setViewRange(double xMin, double xMax);
    void setCalibration(float a0, float a1, float a2);

    /// Push a live cumulative spectrum snapshot (device accumulation).
    void pushSpectrum(const QVector<quint32> &counts, quint32 durationSec);

    /// Background for Net mode (empty counts = no BG / force Live display).
    void setBackground(const QVector<quint32> &counts, quint32 durationSec);
    void clearBackground();

    /// Live vs Net waterfall (Net requires a background).
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_mode; }

    /// Each display row covers this many spectrum polls (1 = every poll).
    /// Higher values lengthen history (~N×) with coarser time resolution.
    void setIntegrateCount(int n);
    int integrateCount() const { return m_integrate; }

    void clear();
    void setMaxRows(int rows);

    /// Vertical highlight linked from spectrum hover (−1 = none). Does not emit signals.
    void setLinkedChannel(int channel);

signals:
    /// channel < 0 when cursor leaves the plot / no data.
    /// rateCps = ΔN/Δt in that channel; deltaCounts = ΔN over the row interval;
    /// liveTimeSec = device spectrum live time at end of interval;
    /// ageFromNewestSec ≈ seconds before the newest row (sum of later intervals).
    void cursorInfoChanged(int channel, double energyKeV, float rateCps, quint32 deltaCounts,
                           quint32 liveTimeSec, int ageFromNewestSec);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Snapshot {
        QVector<quint32> counts;
        quint32 durationSec = 0;
        QDateTime wallTime;
    };

    struct Row {
        QVector<float> rates;       // cps per channel
        QVector<quint32> deltas;    // ΔN per channel (live or net)
        quint32 liveTimeSec = 0;    // device durationSec at end of interval
        quint32 intervalSec = 0;    // Δt for this row
        QDateTime wallTime;
    };

    QRect plotRect() const;
    bool hasEnergyAxis() const;
    double channelToEnergy(double channel) const;
    QRgb rateToColor(float rate) const;
    void rebuildImage();
    void rebuildRowsFromSnapshots();
    QVector<quint32> netCountsFor(const Snapshot &snap) const;
    void clearCursor();
    void setCursorFromPos(const QPoint &pos);
    int rowAtPlotY(int y, const QRect &plot) const;
    double channelAtPlotX(int x, const QRect &plot) const;
    int ageFromNewestSec(int rowIndex) const;
    void drawCursor(QPainter &p, const QRect &plot) const;
    bool netModeActive() const;

    static constexpr int kMarginLeft = 64;
    static constexpr int kMarginRight = 14;
    static constexpr int kMarginTop = 4;
    static constexpr int kMarginBottom = 22;
    static constexpr int kDefaultMaxRows = 240;
    static constexpr int kMaxIntegrate = 32;

    int maxSnaps() const { return m_maxRows * m_integrate + 1; }

    QVector<Snapshot> m_snaps; // newest at back (includes first baseline snap)
    QVector<Row> m_rows;       // rate rows (integrate bins)
    int m_maxRows = kDefaultMaxRows;
    int m_integrate = 1; // polls per display row
    int m_channels = 0;

    DisplayMode m_mode = DisplayMode::Live;
    QVector<quint32> m_bgCounts;
    quint32 m_bgDurationSec = 0;

    double m_xMin = 0;
    double m_xMax = 1;
    float m_a0 = 0;
    float m_a1 = 0;
    float m_a2 = 0;

    float m_displayMax = 1.0f;
    QImage m_image;

    int m_cursorCh = -1;
    int m_cursorRow = -1;
    int m_linkedCh = -1;
};
