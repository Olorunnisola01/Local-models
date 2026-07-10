#include "EqPanel.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "EqBandWidget.h"

// ---------------------------------------------------------------------------
// Built-in preset gains (dB) for bands: 60 150 400 1k 2.5k 6k 12kHz
// ---------------------------------------------------------------------------
const std::array<EqPanel::Gains, EqPanel::kNumBuiltins> EqPanel::kBuiltinPresets = {{
    { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f}, // Flat
    { 8.0f,  5.0f,  2.0f,  0.0f, -1.0f,  0.0f,  0.0f}, // Bass Boost
    { 0.0f,  0.0f,  0.0f,  2.0f,  3.0f,  6.0f,  8.0f}, // Treble Boost
    {-2.0f, -1.0f,  1.0f,  4.0f,  5.0f,  3.0f,  1.0f}, // Vocal Enhance
    { 4.0f,  3.0f,  1.0f,  0.0f, -1.0f, -2.0f, -3.0f}, // Warm
    { 6.0f,  4.0f, -2.0f, -4.0f, -2.0f,  4.0f,  6.0f}, // Funk
    {-3.0f, -4.0f, -1.0f,  4.0f,  6.0f,  4.0f,  2.0f}, // Podcast
}};

const std::array<const char*, EqPanel::kNumBuiltins> EqPanel::kBuiltinNames = {{
    "Flat", "Bass Boost", "Treble Boost", "Vocal Enhance", "Warm", "Funk", "Podcast",
}};

EqPanel::EqPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);

    auto* group = new QGroupBox("Equalizer", this);
    auto* row = new QHBoxLayout(group);

    for (int i = 0; i < tts::GraphicEq::kNumBands; ++i) {
        auto* band = new EqBandWidget(tts::GraphicEq::kCenterFreqs[i], group);
        bands_[i] = band;
        row->addWidget(band);
        connect(band, &EqBandWidget::gainChanged, this, &EqPanel::gainsChanged);
    }
    outer->addWidget(group);

    // -- Preset row: [Preset: combo] [stretch] [Reset EQ] --
    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel("Preset:", this));
    presetCombo_ = new QComboBox(this);
    for (const char* name : kBuiltinNames) {
        presetCombo_->addItem(QString::fromUtf8(name));
    }
    // User slots (labels refreshed after loading saved values)
    for (int s = 0; s < kNumUserSlots; ++s) {
        presetCombo_->addItem(QString("User %1").arg(s + 1));
        userPresets_[s] = kBuiltinPresets[0]; // default flat until loaded
    }

    loadUserPresetsFromSettings();
    refreshComboUserLabels();

    connect(presetCombo_, &QComboBox::currentIndexChanged, this, &EqPanel::applyPreset);
    presetRow->addWidget(presetCombo_);
    presetRow->addStretch(1);

    auto* resetBtn = new QPushButton("Reset EQ", this);
    connect(resetBtn, &QPushButton::clicked, this, &EqPanel::reset);
    presetRow->addWidget(resetBtn);
    outer->addLayout(presetRow);

    // -- Save row: [Save to User 1] [Save to User 2] [Save to User 3] --
    auto* saveRow = new QHBoxLayout();
    for (int s = 0; s < kNumUserSlots; ++s) {
        auto* btn = new QPushButton(QString("Save → User %1").arg(s + 1), this);
        saveUserBtns_[s] = btn;
        connect(btn, &QPushButton::clicked, this, [this, s]() { saveUserSlot(s); });
        saveRow->addWidget(btn);
    }
    saveRow->addStretch(1);
    outer->addLayout(saveRow);
}

std::array<float, tts::GraphicEq::kNumBands> EqPanel::getGainsDb() const {
    std::array<float, tts::GraphicEq::kNumBands> gains{};
    for (int i = 0; i < tts::GraphicEq::kNumBands; ++i) {
        gains[i] = bands_[i]->gainDb();
    }
    return gains;
}

void EqPanel::setGainsDb(const std::array<float, tts::GraphicEq::kNumBands>& gains) {
    for (int i = 0; i < tts::GraphicEq::kNumBands; ++i) {
        bands_[i]->setGainDb(gains[i]);
    }
}

void EqPanel::reset() {
    for (auto* band : bands_) {
        band->setGainDb(0.0f);
    }
    // Deselect preset combo so re-selecting the same preset still fires
    const QSignalBlocker blocker(presetCombo_);
    presetCombo_->setCurrentIndex(-1);
    emit gainsChanged();
}

void EqPanel::applyPreset(int comboIndex) {
    if (comboIndex < 0) return;

    Gains gains;
    if (comboIndex < kNumBuiltins) {
        gains = kBuiltinPresets[comboIndex];
    } else {
        const int slot = comboIndex - kNumBuiltins;
        if (slot >= kNumUserSlots) return;
        gains = userPresets_[slot];
    }

    // Block individual band signals while we set all gains at once, then
    // emit a single gainsChanged at the end.
    for (int i = 0; i < tts::GraphicEq::kNumBands; ++i) {
        const QSignalBlocker blocker(bands_[i]);
        bands_[i]->setGainDb(gains[i]);
    }
    emit gainsChanged();
}

void EqPanel::saveUserSlot(int slot) {
    userPresets_[slot] = getGainsDb();
    saveUserPresetToSettings(slot);
    refreshComboUserLabels();
}

void EqPanel::loadUserPresetsFromSettings() {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    for (int s = 0; s < kNumUserSlots; ++s) {
        for (int b = 0; b < tts::GraphicEq::kNumBands; ++b) {
            const QString key = QString("eq/user%1_band%2").arg(s).arg(b);
            userPresets_[s][b] = settings.value(key, 0.0f).toFloat();
        }
    }
}

void EqPanel::saveUserPresetToSettings(int slot) {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    for (int b = 0; b < tts::GraphicEq::kNumBands; ++b) {
        settings.setValue(QString("eq/user%1_band%2").arg(slot).arg(b), userPresets_[slot][b]);
    }
}

void EqPanel::refreshComboUserLabels() {
    const QSignalBlocker blocker(presetCombo_);
    for (int s = 0; s < kNumUserSlots; ++s) {
        const int comboIdx = kNumBuiltins + s;
        // Show a * if the slot has been saved (not all-zero)
        bool saved = false;
        for (float v : userPresets_[s]) {
            if (v != 0.0f) { saved = true; break; }
        }
        presetCombo_->setItemText(comboIdx,
            saved ? QString("User %1 *").arg(s + 1) : QString("User %1").arg(s + 1));
    }
}
