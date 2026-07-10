#pragma once

#include <array>
#include <string>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

#include "../core/TtsTypes.h"
#include "../core/VoiceCatalog.h"
#include "../dsp/GraphicEq.h"

class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QLabel;
class QPushButton;
class QButtonGroup;
class QProgressBar;
class SpeakerVoiceCard;
class TagInserter;
class ProviderHintsPanel;
class TextStatsPanel;

// A single line of dialogue resolved to a concrete provider + voice
// (with optional 2-voice mixing), plus that speaker's speed and EQ.
struct DialogueSegment {
    tts::Provider provider;
    tts::VoiceEntry voiceA;
    bool mixEnabled = false;
    tts::VoiceEntry voiceB;
    int pctA = 100;
    float speed = 1.0f;
    std::array<float, tts::GraphicEq::kNumBands> eqGainsDb{};
    bool humanizerEnabled = false; // per-speaker Natural Humanizer (Edge)
    std::string text;
};

// Snapshot of one speaker's voice/mix/speed/EQ configuration, kept around
// while the shared SpeakerVoiceCard editor is showing a different speaker.
struct SpeakerSettings {
    tts::Provider provider = tts::Provider::Supertonic;
    int voiceAIndex = 0;
    bool mixEnabled = false;
    int voiceBIndex = 1;
    int pctA = 50;
    float speed = 1.05f;
    std::array<float, tts::GraphicEq::kNumBands> eqGainsDb{};
    bool humanizerEnabled = false; // per-speaker Natural Humanizer (Edge)
};

// A full "Render All" request from the Multi-Speaker tab.
struct MultiSynthRequest {
    std::vector<DialogueSegment> segments;
    int pauseMs = 220;
};

// Multi-Speaker Dialogue tab: a script editor where lines are written as
// "A: ...", "B: ...", etc, a speaker-count selector, a speaker switcher
// (A-E), and a single shared voice/mix/speed/EQ editor that is saved and
// reloaded as the selected speaker changes. Rendering, playback, and export
// are handled by MainWindow via signals.
class MultiSpeakerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MultiSpeakerPanel(QWidget* parent = nullptr);

    // Disables render/preview while a render is in progress; enables Cancel.
    void setBusy(bool busy);
    DialogueSegment currentSpeakerLine(const QString& text) const;
    void setChunkSettings(int maxChunkChars, int sentenceGapMs, int paragraphGapMs);
    QPushButton* previewButton() const;
    // Enables/disables Play/Pause/Stop/Export once rendered audio is available.
    void setPlaybackEnabled(bool enabled);
    // Updates the Pause/Resume button label to reflect playback state.
    void setPauseLabel(const QString& text);
    void setStatus(const QString& text);
    void setProgress(int percent);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);
    void resetToDefaults();

signals:
    void renderRequested(const MultiSynthRequest& req);
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void exportRequested();
    void enhanceRequested();
    void cancelRequested();
    void previewSelectionRequested(const QString& text);
    void importTextRequested();

private:
    static constexpr int kMaxSpeakers = 5; // A-E

    void onRenderClicked();
    void onPreviewClicked();
    void onImportClicked();
    void onScriptChanged();
    void onSpeakerCountChanged(int count);
    void onSpeakerButtonClicked(int index);
    void saveEditorToSettings(int index);
    void loadSettingsToEditor(int index);
    std::vector<DialogueSegment> parseScript() const;

    QPlainTextEdit* scriptEdit_;
    TagInserter* tagInserter_ = nullptr;
    ProviderHintsPanel* providerHints_ = nullptr;
    TextStatsPanel* textStats_ = nullptr;
    QPushButton* previewBtn_ = nullptr;
    QPushButton* importBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;

    QSpinBox* speakerCountSpin_;
    QButtonGroup* speakerButtonGroup_;
    std::array<QPushButton*, kMaxSpeakers> speakerButtons_{};
    SpeakerVoiceCard* speakerEditor_;
    std::array<SpeakerSettings, kMaxSpeakers> speakerSettings_{};
    int currentSpeaker_ = 0;

    QSlider* pauseSlider_;
    QLabel* pauseLabel_;
    QPushButton* renderBtn_;
    int maxChunkChars_ = 400;
    int sentenceGapMs_ = 150;
    int paragraphGapMs_ = 600;
    QPushButton* playBtn_;
    QPushButton* pauseBtn_;
    QPushButton* stopBtn_;
    QPushButton* exportBtn_;
    QPushButton* enhanceBtn_;
    QLabel* statusLabel_;
    QProgressBar* progressBar_;
};
