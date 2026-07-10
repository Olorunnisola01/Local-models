#include "EqBandWidget.h"

#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

namespace {
QString freqLabel(float hz) {
    if (hz >= 1000.0f) {
        return QString("%1k").arg(hz / 1000.0f, 0, 'g', 3);
    }
    return QString("%1").arg(static_cast<int>(hz));
}
} // namespace

EqBandWidget::EqBandWidget(float freqHz, QWidget* parent) : QWidget(parent), freqHz_(freqHz) {
    auto* layout = new QVBoxLayout(this);

    auto* topLabel = new QLabel(freqLabel(freqHz_) + " Hz", this);
    topLabel->setAlignment(Qt::AlignHCenter);

    valueLabel_ = new QLabel("0.0 dB", this);
    valueLabel_->setAlignment(Qt::AlignHCenter);

    slider_ = new QSlider(Qt::Vertical, this);
    slider_->setRange(-120, 120); // -12.0..+12.0 dB in 0.1 dB steps
    slider_->setValue(0);
    slider_->setTickPosition(QSlider::TicksBothSides);
    slider_->setTickInterval(60);
    slider_->setMinimumHeight(160);

    layout->addWidget(topLabel);
    layout->addWidget(valueLabel_);
    layout->addWidget(slider_, /*stretch=*/1, Qt::AlignHCenter);
    layout->setAlignment(Qt::AlignHCenter);

    connect(slider_, &QSlider::valueChanged, this, &EqBandWidget::onSliderChanged);
}

float EqBandWidget::gainDb() const {
    return slider_->value() / 10.0f;
}

void EqBandWidget::setGainDb(float db) {
    slider_->setValue(static_cast<int>(db * 10.0f));
}

void EqBandWidget::onSliderChanged(int value) {
    float db = value / 10.0f;
    valueLabel_->setText(QString::number(db, 'f', 1) + " dB");
    emit gainChanged(db);
}
