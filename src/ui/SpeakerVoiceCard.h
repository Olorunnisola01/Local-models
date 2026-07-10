#pragma once

#include <array>

#include <QGroupBox>

#include "../core/TtsTypes.h"
#include "../core/VoiceCatalog.h"
#include "../dsp/GraphicEq.h"

class QComboBox;
class QCheckBox;
class QDialog;
class QPushButton;
class QSlider;
class QLabel;
class EqPanel;

// One speaker's voice configuration in the Multi-Speaker Dialogue tab:
// provider + voice (with optional 2-voice mixing), a per-speaker speed, and
// a per-speaker 7-band EQ.
class SpeakerVoiceCard : public QGroupBox {
    Q_OBJECT
public:
    explicit SpeakerVoiceCard(const QString& title, QWidget* parent = nullptr);

    tts::Provider provider() const;
    tts::VoiceEntry voiceA() const;
    bool mixEnabled() const;
    tts::VoiceEntry voiceB() const;
    int pctA() const;
    float speed() const;
    std::array<float, tts::GraphicEq::kNumBands> eqGainsDb() const;
    // Per-speaker Natural Humanizer (Edge voices only).
    bool humanizerEnabled() const;
    void setHumanizerEnabled(bool enabled);

    // Index-based accessors/mutators, used to snapshot/restore this card's
    // configuration when a single shared editor switches between speakers.
    int voiceAIndex() const;
    int voiceBIndex() const;
    void setProvider(tts::Provider provider);
    void setVoiceAIndex(int index);
    void setMixEnabled(bool enabled);
    void setVoiceBIndex(int index);
    void setPctA(int pct);
    void setSpeed(float speed);
    void setEqGainsDb(const std::array<float, tts::GraphicEq::kNumBands>& gains);

signals:
    void providerIndexChanged(int index);
    void editorSettingsChanged();

private:
    void populateVoiceCombos();
    void updateMixUiEnabled();
    void updateVoiceFilterUi();   // shows gender/lang filters + humanizer for Edge only
    void refreshPresetCombo();    // reloads the global voice-preset list
    void applyPreset(int comboIndex);

    QComboBox* providerCombo_;
    QWidget*   filterWidget_ = nullptr; // gender+language filters (Edge only)
    QComboBox* genderFilter_ = nullptr;
    QComboBox* langFilter_ = nullptr;
    QComboBox* voiceCombo_;
    QCheckBox* mixCheckBox_;
    QComboBox* voiceBCombo_;
    QSlider* mixSlider_;
    QLabel* mixLabel_;
    QSlider* speedSlider_;
    QLabel* speedLabel_;
    QComboBox* presetCombo_ = nullptr;   // apply a saved voice preset to this speaker
    QCheckBox* humanizerCheck_ = nullptr; // per-speaker Natural Humanizer (Edge)
    EqPanel*     eqPanel_;
    QDialog*     eqDialog_;
    QPushButton* eqDialogBtn_;
};
