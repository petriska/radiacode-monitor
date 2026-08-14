#pragma once

#include <QDateTime>
#include <QImage>
#include <QPoint>
#include <QVector>
#include <QWidget>

class SpectrogramRecorder;

// Time × energy spectrogram / waterfall under the spectrum (SDR-style).
//
// History: ring of rate rows (ΔN/Δt) with wall-clock timestamps. Capacity is
// set in minutes (default 2 h) and grows with integrate coarseness.
// Display: 1:1 time (one row = one pixel), bottom-aligned; wheel scrolls
// through history. scrollFromNewest==0 follows live data.
// Colour map = [floor, ceil] within auto max over history; recolour when max moves (~2%).
// Right-hand SDR-style colour bar: drag / wheel to scale the mapped range (highlight weak rates).
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
    /// Energy calibration currently used for cursors / extract / captions (from device or .rcsg).
    float calibA0() const { return m_a0; }
    float calibA1() const { return m_a1; }
    float calibA2() const { return m_a2; }
    bool hasEnergyCalibration() const;

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

    /// Colour scale: map rates in [floor, ceil] × autoMax onto the palette (SDR-style).
    /// ceilFraction 0.02…2.0 (1.0 = full auto max; lower = more sensitive / “gain”).
    /// floorFraction 0…0.5 (cut noise floor).
    void setColorCeilFraction(float frac);
    float colorCeilFraction() const { return m_colorCeilFrac; }
    void setColorFloorFraction(float frac);
    float colorFloorFraction() const { return m_colorFloorFrac; }
    void resetColorScale();
    /// Auto matrix max used as the reference for floor/ceil (cps).
    float autoDisplayMax() const { return m_displayMax; }

    /// Device serial (and optional label) for PNG export metadata.
    void setDeviceSerial(const QString &serial);
    QString deviceSerial() const { return m_deviceSerial; }

    /// Lossless PNG from **rate data** (not a widget screenshot).
    /// View = visible time window × current energy zoom; Full = entire history buffer.
    enum class PngExportScope { View, FullHistory };
    bool exportAsPng(PngExportScope scope, QWidget *dialogParent = nullptr);

    /// Save / load full history buffer as binary .rcsg (rates + timestamps + meta).
    bool saveHistory(QWidget *dialogParent = nullptr);
    bool loadHistory(QWidget *dialogParent = nullptr);

    /// Optional continuous recorder (not owned). Appends each new history row while set.
    void setRecorder(SpectrogramRecorder *recorder);
    SpectrogramRecorder *recorder() const { return m_recorder; }

    /// Rectangle selection in buffer coordinates (E1). Channels [ch0, ch1), rows [row0, row1].
    struct Selection {
        int ch0 = 0;
        int ch1 = 0;   // exclusive
        int row0 = 0;  // inclusive (oldest side of box)
        int row1 = 0;  // inclusive (newest side of box)
        bool valid = false;
    };
    bool hasSelection() const { return m_selection.valid; }
    Selection selection() const { return m_selection; }
    void clearSelection();

    /// Integrated ΔN per channel over the selected time rows (all channels; same live window).
    /// Energy highlight for the selection ch window is applied by the dialog (blue vs gray).
    struct SelectionSpectrum {
        QVector<quint32> counts;
        quint32 durationSec = 0; // sum of intervalSec over selected rows
        float a0 = 0;
        float a1 = 0;
        float a2 = 0;
    };
    /// One MCS sample at a history row (same time window for full + selection).
    struct SelectionMcsPoint {
        QDateTime wallTime;
        quint32 liveTimeSec = 0;
        quint32 intervalSec = 0;
        double fullCps = 0;      // all channels (no energy window)
        quint64 fullCounts = 0;  // sum of ΔN over all channels
        double cps = 0;          // selection energy window only
        quint64 counts = 0;      // sum of ΔN in ch window
    };

    SelectionSpectrum extractSelectionSpectrum() const;
    QVector<SelectionMcsPoint> extractSelectionMcs() const;

    void clear();

    /// Vertical highlight linked from spectrum hover (−1 = none). Does not emit signals.
    void setLinkedChannel(int channel);

signals:
    /// channel < 0 when cursor leaves the plot / no data.
    void cursorInfoChanged(int channel, double energyKeV, float rateCps, quint32 deltaCounts,
                           quint32 liveTimeSec, int ageFromNewestSec);
    void followLiveChanged(bool following);
    void scrollChanged(int scrollFromNewest, int maxScroll);
    /// Colour scale floor/ceil fractions changed (UI sync).
    void colorScaleChanged(float floorFrac, float ceilFrac);
    /// Emitted when the analysis rectangle is set, cleared, or adjusted after history trim.
    void selectionChanged(bool hasSelection, int ch0, int ch1, int row0, int row1);
    void extractSpectrumRequested();
    void extractMcsRequested();
    void exportSelectionSpectrumRequested();
    void exportSelectionMcsRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

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
    float colorMapMin() const;
    float colorMapMax() const;
    QRect colorBarRect() const;
    void drawColorBar(QPainter &p, const QRect &plot) const;
    void applyColorScaleFromBarY(int y, bool floorHandle);
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
    void drawSelection(QPainter &p, const QRect &plot) const;
    bool netModeActive() const;
    void emitScrollSignals();
    void emitSelectionChanged();
    /// Map two widget points to channel/row selection. notify=false while dragging.
    void setSelectionFromCorners(const QPoint &a, const QPoint &b, bool notify = true);
    void moveSelectionBy(int dCh, int dRow, bool notify);
    void resizeSelectionEdge(int edges, const QPoint &pos, bool notify);
    void adjustSelectionAfterHistoryTrim(int dropped);
    QRect selectionPixelRect(const TimeView &tv) const;
    bool selectionContainsWidgetPos(const QPoint &pos) const;
    int rowFromWidgetY(int y, const TimeView &tv) const;
    /// Bit flags: Left=1 Right=2 Top=4 Bottom=8; 0 = none / inside handled separately.
    int hitTestSelectionEdge(const QPoint &pos) const;
    void applySelectionHoverCursor(const QPoint &pos);
    /// Rasterize rate rows [firstRow, firstRow+nRows) and channels [ch0, ch1) → RGB image.
    QImage renderRatesToImage(int firstRow, int nRows, int ch0, int ch1) const;
    void applyPngMetadata(QImage *img, int firstRow, int lastRow, int ch0, int ch1,
                          PngExportScope scope) const;

    static constexpr int kMarginLeft = 64;
    static constexpr int kMarginRight = 28; // room for SDR-style colour bar
    static constexpr int kMarginTop = 4;
    static constexpr int kMarginBottom = 22;
    static constexpr int kColorBarWidth = 14;
    static constexpr int kColorBarGap = 4;
    static constexpr float kColorCeilMin = 0.02f;
    static constexpr float kColorCeilMax = 2.0f;
    static constexpr float kColorFloorMax = 0.5f;
    static constexpr int kMinHistoryMinutes = 15;
    static constexpr int kMaxHistoryMinutes = 8 * 60;
    static constexpr int kDefaultHistoryMinutes = 120; // 2 h
    static constexpr int kMaxIntegrate = 32;
    static constexpr int kMinRows = 64;
    static constexpr int kMaxRowsCap = 28800; // 8 h @ 1 s / row
    static constexpr float kScaleHysteresis = 0.02f;
    static constexpr int kSelectDragThresholdPx = 4;
    static constexpr int kSelectEdgeHitPx = 7;
    static constexpr int kEdgeLeft = 1;
    static constexpr int kEdgeRight = 2;
    static constexpr int kEdgeTop = 4;
    static constexpr int kEdgeBottom = 8;

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

    float m_displayMax = 1.0f; // auto max over history (reference)
    float m_colorCeilFrac = 1.0f;  // colour map top = displayMax * ceil
    float m_colorFloorFrac = 0.0f; // colour map bottom
    bool m_colorBarDragging = false;
    bool m_colorBarDragFloor = false; // false = drag ceil (top); true = floor (bottom)
    /// Viewport-sized colour cache (not the full multi-hour buffer).
    QImage m_image;
    int m_viewportFirstRow = -1; // first history row currently baked into m_image
    int m_viewportCount = 0;

    int m_cursorCh = -1;
    int m_cursorRow = -1;
    int m_linkedCh = -1;

    QString m_deviceSerial;
    SpectrogramRecorder *m_recorder = nullptr; // not owned

    // Analysis rectangle (buffer coords). E1 — extract spectrum/MCS in E2.
    Selection m_selection;
    bool m_selectDragging = false;   // creating a new box
    bool m_selectMoving = false;     // translating existing box
    bool m_selectResizing = false;   // dragging an edge/corner
    bool m_selectDragMoved = false;
    QPoint m_selectPressPos;
    QPoint m_selectCurrPos;
    Selection m_moveOrig;            // selection at move/resize start
    int m_movePressCh = 0;
    int m_movePressRow = 0;
    int m_resizeEdges = 0;           // kEdge* bit flags
};
