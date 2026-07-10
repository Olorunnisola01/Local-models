#pragma once

#include <array>

#include <QWidget>

#include "../dsp/GraphicEq.h"

class EqBandWidget;
class QComboBox;
class QPushButton;

// A row of EqBandWidget faders (one per GraphicEq band), a preset combo
// (7 built-ins + 3 user-saveable slots), Save buttons, and a Reset button.
class EqPanel : public QWidget {
    Q_OBJECT
public:
    explicit EqPanel(QWidget* parent = nullptr);

    std::array<float, tts::GraphicEq::kNumBands> getGainsDb() const;
    void setGainsDb(const std::array<float, tts::GraphicEq::kNumBands>& gains);
    void reset();

signals:
    void gainsChanged();

private:
    static constexpr int kNumBuiltins = 7;
    static constexpr int kNumUserSlots = 3;

    void applyPreset(int comboIndex);
    void saveUserSlot(int slot); // 0-based

    std::array<EqBandWidget*, tts::GraphicEq::kNumBands> bands_{};
    QComboBox* presetCombo_ = nullptr;

    using Gains = std::array<float, tts::GraphicEq::kNumBands>;
    static const std::array<Gains, kNumBuiltins> kBuiltinPresets;
    static const std::array<const char*, kNumBuiltins> kBuiltinNames;

    std::array<Gains, kNumUserSlots> userPresets_{};
    std::array<QPushButton*, kNumUserSlots> saveUserBtns_{};

    void loadUserPresetsFromSettings();
    void saveUserPresetToSettings(int slot);
    void refreshComboUserLabels();
};
