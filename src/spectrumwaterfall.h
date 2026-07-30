#pragma once

#include <QDateTime>
#include <QImage>
#include <QVector>
#include <QWidget>

// Time × energy waterfall under the spectrum (SDR-style).
// Each row is count-rate per channel since the previous spectrum (ΔN / Δt).
// Newest row at the bottom; older rows scroll upward.
class SpectrumWaterfall : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWaterfall(QWidget *parent = nullptr);

    /// Visible channel range [xMin, xMax) — typically mirrored from SpectrumWidget zoom.
    void setViewRange(double xMin, double xMax);
    void setCalibration(float a0, float a1, float a2);

    /// Push a live cumulative spectrum snapshot (device accumulation).
    void pushSpectrum(const QVector<quint32> &counts, quint32 durationSec);

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
    struct Row {
        QVector<float> rates;       // cps per channel
        QVector<quint32> deltas;    // ΔN per channel
        quint32 liveTimeSec = 0;    // device durationSec at end of interval
        quint32 intervalSec = 0;    // Δt for this row
        QDateTime wallTime;         // host clock when row was added
    };

    QRect plotRect() const;
    bool hasEnergyAxis() const;
    double channelToEnergy(double channel) const;
    QRgb rateToColor(float rate) const;
    void rebuildImage();
    void appendRow(Row &&row);
    void clearCursor();
    void setCursorFromPos(const QPoint &pos);
    /// Map widget Y in plot → row index in m_rows, or -1.
    int rowAtPlotY(int y, const QRect &plot) const;
    double channelAtPlotX(int x, const QRect &plot) const;
    int ageFromNewestSec(int rowIndex) const;
    void drawCursor(QPainter &p, const QRect &plot) const;

    static constexpr int kMarginLeft = 64;
    static constexpr int kMarginRight = 14;
    static constexpr int kMarginTop = 4;
    static constexpr int kMarginBottom = 22;
    static constexpr int kDefaultMaxRows = 240;

    QVector<Row> m_rows; // newest at back
    int m_maxRows = kDefaultMaxRows;
    int m_channels = 0;

    QVector<quint32> m_prevCounts;
    quint32 m_prevDurationSec = 0;
    bool m_havePrev = false;

    double m_xMin = 0;
    double m_xMax = 1;
    float m_a0 = 0;
    float m_a1 = 0;
    float m_a2 = 0;

    float m_displayMax = 1.0f;
    QImage m_image;

    int m_cursorCh = -1;
    int m_cursorRow = -1; // index in m_rows
    int m_linkedCh = -1;  // external link (spectrum)
};
