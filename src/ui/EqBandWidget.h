#pragma once

#include <QWidget>

class QSlider;
class QLabel;

// One vertical fader: frequency label on top, dB readout below it, a
// vertical slider spanning -12.0..+12.0 dB (0.1 dB steps), and the
// frequency label repeated at the bottom for readability in a long row.
class EqBandWidget : public QWidget {
    Q_OBJECT
public:
    explicit EqBandWidget(float freqHz, QWidget* parent = nullptr);

    float gainDb() const;
    void setGainDb(float db);

signals:
    void gainChanged(float db);

private slots:
    void onSliderChanged(int value);

private:
    QSlider* slider_;
    QLabel* valueLabel_;
    float freqHz_;
};
