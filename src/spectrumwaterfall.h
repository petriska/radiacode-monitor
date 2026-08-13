#pragma once

#include <QDateTime>
#include <QImage>
#include <QVector>
#include <QWidget>

// Time × energy spectrogram / waterfall under the spectrum (SDR-style).
//
// History: ring of rate rows (ΔN/Δt) with wall-clock timestamps. Capacity is
// set in minutes (default 2 h) and grows with integrate coarseness.
// Display: 1:1 time (one row = one pixel), bottom-aligned; wheel scrolls
// through history. scrollFromNewest==0 follows live data.
// Colour map = [0, max over full history]; recolour when max moves (~2%).
// Net view adjusts live rates by BG rate at paint time (no full snap history).
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
    void setIntegrateCount(int n);
    int integrateCount() const { return m_integrate; }

    /// Target history depth in minutes (15…8*60). Capacity in rows depends on integrate.
    void setHistoryMinutes(int minutes);
    int historyMinutes() const { return m_historyMinutes; }
    int maxRows() const { return m_maxRows; }
    int rowCount() const { return m_rows.size(); }

    /// 0 = follow newest (live). Larger = look further into the past.
    void setScrollFromNewest(int rows);
    int scrollFromNewest() const { return m_scrollFromNewest; }
    bool isFollowingLive() const { return m_scrollFromNewest == 0; }
    void followLive();
    /// Jump viewport to the oldest data still in the history buffer.
    void goToOldest();

    /// Device serial (and optional label) for PNG export metadata.
    void setDeviceSerial(const QString &serial);
    QString deviceSerial() const { return m_deviceSerial; }

    /// Export the current on-screen spectrogram view as a lossless PNG (file dialog).
    bool exportViewAsPng(QWidget *dialogParent = nullptr);

    void clear();

    /// Vertical highlight linked from spectrum hover (−1 = none). Does not emit signals.
    void setLinkedChannel(int channel);

signals:
    /// channel < 0 when cursor leaves the plot / no data.
    void cursorInfoChanged(int channel, double energyKeV, float rateCps, quint32 deltaCounts,
                           quint32 liveTimeSec, int ageFromNewestSec);
    void followLiveChanged(bool following);
    void scrollChanged(int scrollFromNewest, int maxScroll);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct Snapshot {
        QVector<quint32> counts;
        quint32 durationSec = 0;
        QDateTime wallTime;
    };

    struct Row {
        QVector<float> rates;       // live cps per channel
        QVector<quint32> deltas;    // live ΔN per channel
        quint32 liveTimeSec = 0;
        quint32 intervalSec = 0;
        QDateTime wallTime;
    };

    /// 1:1 bottom-aligned time view into the history buffer.
    struct TimeView {
        QRect imgRect;
        QRect dest;
        int visible = 0;
        int firstRow = 0;
        int lastRow = 0; // inclusive
    };

    QRect plotRect() const;
    bool hasEnergyAxis() const;
    double channelToEnergy(double channel) const;
    float bgRate(int ch) const;
    float displayRate(float liveRate, int ch) const;
    QRgb rateToColor(float rate) const;
    void recomputeCapacity();
    void clampScroll();
    int maxScroll() const;
    void ensureViewportImage(int visibleRows);
    void rebuildViewportImage();
    void appendDisplayRow(Row &&row);
    bool makeRow(const Snapshot &prev, const Snapshot &cur, Row *out) const;
    float matrixMaxRate() const;
    bool timeView(const QRect &plot, TimeView *tv) const;
    void refreshCursorAfterScroll(bool droppedOldest);
    void clearCursor();
    void setCursorFromPos(const QPoint &pos);
    int rowAtPlotY(int y, const QRect &plot) const;
    double channelAtPlotX(int x, const QRect &plot) const;
    int ageFromNewestSec(int rowIndex) const;
    void drawCursor(QPainter &p, const QRect &plot) const;
    bool netModeActive() const;
    void emitScrollSignals();
    void applyPngMetadata(QImage *img) const;

    static constexpr int kMarginLeft = 64;
    static constexpr int kMarginRight = 14;
    static constexpr int kMarginTop = 4;
    static constexpr int kMarginBottom = 22;
    static constexpr int kMinHistoryMinutes = 15;
    static constexpr int kMaxHistoryMinutes = 8 * 60;
    static constexpr int kDefaultHistoryMinutes = 120; // 2 h
    static constexpr int kMaxIntegrate = 32;
    static constexpr int kMinRows = 64;
    static constexpr int kMaxRowsCap = 28800; // 8 h @ 1 s / row
    static constexpr float kScaleHysteresis = 0.02f;

    QVector<Row> m_rows; // oldest at front, newest at back
    int m_historyMinutes = kDefaultHistoryMinutes;
    int m_maxRows = 7200;
    int m_integrate = 1;
    int m_channels = 0;
    int m_scrollFromNewest = 0;

    Snapshot m_baseline;
    bool m_hasBaseline = false;
    int m_integrateProgress = 0;

    DisplayMode m_mode = DisplayMode::Live;
    QVector<quint32> m_bgCounts;
    quint32 m_bgDurationSec = 0;

    double m_xMin = 0;
    double m_xMax = 1;
    float m_a0 = 0;
    float m_a1 = 0;
    float m_a2 = 0;

    float m_displayMax = 1.0f;
    /// Viewport-sized colour cache (not the full multi-hour buffer).
    QImage m_image;
    int m_viewportFirstRow = -1; // first history row currently baked into m_image
    int m_viewportCount = 0;

    int m_cursorCh = -1;
    int m_cursorRow = -1;
    int m_linkedCh = -1;

    QString m_deviceSerial;
};
