#pragma once

#include <QVector>
#include <QWidget>

// Simple bar/line spectrum display (no external plot dependency).
class SpectrumWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget *parent = nullptr);

    void setSpectrum(const QVector<quint32> &counts, float a0, float a1, float a2);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<quint32> m_counts;
    float m_a0 = 0;
    float m_a1 = 0;
    float m_a2 = 0;
};
