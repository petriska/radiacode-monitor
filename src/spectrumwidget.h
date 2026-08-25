#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

// Energy or channel band used to tint spectrum bars (ROI panel / selection highlight).
// Prefer channel range when chMin >= 0; otherwise use energy [eMinKeV, eMaxKeV) if calibration is set.
struct SpectrumRoiBand {
    double eMinKeV = 0;
    double eMaxKeV = 0;
    int chMin = -1; // inclusive; if >= 0, use [chMin, chMax) instead of energy
    int chMax = -1; // exclusive
    QColor color;
    bool enabled = true;
};

// Interactive spectrum plot: bars with √ or log1p Y scale, X zoom/pan, energy cursor.
class SpectrumWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget *parent = nullptr);

    void setSpectrum(const QVector<quint32> &counts, float a0, float a1, float a2);
    /// Tint channels that fall inside ROI energy/channel windows; overlapping ROIs blend RGB.
    void setRoiBands(const QVector<SpectrumRoiBand> &bands);
    /// Default bar colour for channels outside ROI bands (and for the whole plot when no ROIs).
    void setBaseBarColor(const QColor &color);
    /// When true, Y uses log(1+counts) so zeros stay valid; otherwise √ scale (default).
    void setLogYScale(bool on);
    bool logYScale() const { return m_logY; }
    void clear();
    void resetView();
    /// Visible channel range [xMin, xMax). Clamped; emits viewRangeChanged.
    void setViewRange(double xMin, double xMax);

    double viewXMin() const { return m_xMin; }
    double viewXMax() const { return m_xMax; }

    /// Vertical highlight linked from waterfall hover (−1 = none). Does not emit signals.
    void setLinkedChannel(int channel);

signals:
    /// Emitted when the cursor channel changes (-1 when outside plot / no data).
    void cursorInfoChanged(int channel, double energyKeV, quint32 counts);
    /// Visible channel range [xMin, xMax) after zoom/pan/reset.
    void viewRangeChanged(double xMin, double xMax);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect plotRect() const;
    bool hasEnergyAxis() const;
    double channelToEnergy(double channel) const;
    double channelToX(double channel, const QRect &plot) const;
    double xToChannel(int x, const QRect &plot) const;
    void clampView();
    void emitViewRange();
    void setCursorFromPos(const QPoint &pos);
    void clearCursor();
    quint32 maxCountInView() const;
    int channelCount() const;
    /// Map counts → [0,1] for bar height (√ or log1p).
    double yNorm(double counts, double maxC) const;
    /// Inverse of yNorm for axis tick labels.
    double yDenorm(double u, double maxC) const;

    void drawBackground(QPainter &p) const;
    void drawGridAndAxes(QPainter &p, const QRect &plot, quint32 maxC) const;
    void drawSpectrum(QPainter &p, const QRect &plot, quint32 maxC) const;
    void drawCursor(QPainter &p, const QRect &plot) const;
    QColor barColorForChannel(int channel, bool highlight) const;

    QVector<quint32> m_counts;
    float m_a0 = 0;
    float m_a1 = 0;
    float m_a2 = 0;
    QVector<SpectrumRoiBand> m_roiBands;
    QColor m_baseBarColor = QColor(70, 150, 255, 210);
    QColor m_baseBarColorHi = QColor(110, 190, 255, 230);
    bool m_logY = false;

    // Visible channel range [m_xMin, m_xMax) in channel units.
    double m_xMin = 0;
    double m_xMax = 1;

    int m_cursorCh = -1;
    int m_linkedCh = -1; // external link (waterfall)
    bool m_panning = false;
    QPoint m_lastPanPos;
};
