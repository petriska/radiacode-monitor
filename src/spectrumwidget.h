#pragma once

#include <QVector>
#include <QWidget>

// Interactive spectrum plot: bars with sqrt Y scale, X zoom/pan, vertical energy cursor.
class SpectrumWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget *parent = nullptr);

    void setSpectrum(const QVector<quint32> &counts, float a0, float a1, float a2);
    void clear();
    void resetView();

signals:
    /// Emitted when the cursor channel changes (-1 when outside plot / no data).
    void cursorInfoChanged(int channel, double energyKeV, quint32 counts);

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
    void setCursorFromPos(const QPoint &pos);
    void clearCursor();
    quint32 maxCountInView() const;
    int channelCount() const;

    void drawBackground(QPainter &p) const;
    void drawGridAndAxes(QPainter &p, const QRect &plot, quint32 maxC) const;
    void drawSpectrum(QPainter &p, const QRect &plot, quint32 maxC) const;
    void drawCursor(QPainter &p, const QRect &plot) const;

    QVector<quint32> m_counts;
    float m_a0 = 0;
    float m_a1 = 0;
    float m_a2 = 0;

    // Visible channel range [m_xMin, m_xMax) in channel units.
    double m_xMin = 0;
    double m_xMax = 1;

    int m_cursorCh = -1;
    bool m_panning = false;
    QPoint m_lastPanPos;
};
