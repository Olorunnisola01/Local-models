#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include <string>

#include <QBuffer>
#include <QByteArray>
#include <QCloseEvent>
#include <QMainWindow>

#include "../core/EdgeTtsEngine.h"
#include "../core/KokoroEngine.h"
#include "../core/Phonemizer.h"
#include "../core/PiperEngine.h"
#include "../core/RemoteFishSpeechEngine.h"
#include "../core/RemoteKokoroEngine.h"
#include "../core/SupertonicEngine.h"
#include "../core/TtsTypes.h"
#include "../core/VoiceCatalog.h"
#include "../core/VoiceStyle.h"
#include "../dsp/DeepFilterNetEngine.h"
#include "../dsp/Humanizer.h"
#include "MultiSpeakerPanel.h"
#include "ProjectManager.h"
#include "../core/TextMarkup.h"
#include "ExportPackageDialog.h"
#include "PronunciationPanel.h"
#include "TimelinePanel.h"
#include "TimelineTypes.h"

class QPlainTextEdit;
class QTabWidget;
class QAction;
class QComboBox;
class QSlider;
class QLabel;
class QPushButton;
class QCheckBox;
class QLineEdit;
class QAudioSink;
class QProgressBar;
class QTimer;
class QMenu;
class EqPanel;
class TagInserter;
class ProviderHintsPanel;
class RemoteGpuPanel;
class BatchQueueDialog;
class TextStatsPanel;

template <typename T>
class QFutureWatcher;

// Main window: text input, provider switcher (Supertonic / Kokoro / Piper),
// voice picker with optional 2-voice mixing, speed control, 7-band graphic
// EQ, and Synthesize/Play/Stop/Export transport.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    // Emitted from the background synthesis thread as long-text rendering
    // progresses through its chunks; connected (auto-queued) to update the
    // progress bars on the UI thread.
    void synthesisProgress(int percent);
    void synthesisStatusMessage(QString message);
    void multiSynthesisProgress(int percent);

private slots:
    void onEngineLoaded();
    void onProviderChanged(int index);
    void onSynthesizeClicked();
    void onSynthesisFinished();
    void onPlayClicked();
    void onStopClicked();
    void onExportClicked();
    void onEqChanged();
    void onMultiRenderRequested(const MultiSynthRequest& req);
    void onMultiSynthesisFinished();
    void onEnhanceClicked();
    void onEnhanceFinished();
    void onGpuToggled(bool checked);
    void onRemoteKokoroToggled(bool checked);
    void onStopKaggleClicked();
    void onStopKaggleFinished();
    void onSettingsClicked();
    void onPauseClicked();
    void onExportFinished();
    void onEqDialogClicked();
    void onHumanizerSettingsClicked();
    void onFishBrowseClicked();
    void onFishClearClicked();
    void onFishSaveSlotClicked();
    void onFishExtractTokensClicked();
    void onFishExtractTokensFinished();
    void onStopFishKaggleClicked();
    void onStopFishKaggleFinished();
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onSaveVoicePreset();
    void onDeleteVoicePreset();
    void onVoicePresetChanged(int index);
    void onTimelineRebuildRequested();
    void onExportPackage();
    void onPronunciationPreviewRequested();
    void onPreviewSelectionClicked();
    void onPreviewFinished();
    void onCancelSynthesisClicked();
    void onOpenRecentProject();
    void onToggleDarkMode();
    void onModelManagerClicked();
    void onBatchQueueClicked();
    void onBatchItemRequested(const QString& text, const QString& outputPath, int itemIndex);
    void onBatchItemExportFinished();
    void onBatchCancelRequested();
    void onTimelineRerenderRequested(int segmentIndex, const QString& text);
    void onTimelineRerenderFinished();
    void onTestEdgeTtsRequested();
    void onTestEdgeTtsFinished();
    void onPartialPlaybackReady();
    void offerAutosaveRecovery();
    void onPlaybackTick();
    void showFirstRunIfNeeded();
    void onVoicePreviewClicked();
    void onImportTextClicked();
    void onAutosaveTick();
    void onMultiPreviewSelectionRequested(const QString& text);
    void onMultiImportText();
    void seekPlayback(double seconds);

private:
    // Snapshot of UI selections, captured on the UI thread before dispatching
    // synthesis to a background thread.
    struct SynthRequest {
        std::string text;
        float speed = 1.0f;
        tts::Provider provider = tts::Provider::Supertonic;
        tts::VoiceEntry voiceA;
        bool mixEnabled = false;
        tts::VoiceEntry voiceB;
        int pctA = 100;
        std::vector<tts::PronunciationEntry> dictionary;
    };

    void setStatus(const QString& text);
    void updateStatusBar();
    void updateGpuStatusLabel();
    bool validateCurrentProvider(const tts::VoiceEntry* voiceA, QString* errorOut) const;
    void setupVoiceCompleter(QComboBox* combo);
    void markProjectDirty();
    void runPreviewSynth(const SynthRequest& req, QPushButton* disableBtn);
    void reprocessAndRefreshPlayback(); // re-applies EQ + normalize to cached raw audio
    std::vector<int16_t> toInt16(const std::vector<float>& samples) const;
    void populateVoiceCombos();
    void updateVoiceFilterUi(); // shows gender/language filters for Edge only
    // Selects the voiceCombo_/voiceBCombo_ item mapping to full-list index
    // `fullIdx`, clearing the gender/language filters first if it's hidden.
    void selectVoiceAByFullIndex(int fullIdx);
    void selectVoiceBByFullIndex(int fullIdx);
    void updateMixUiEnabled();
    // Updates the Fish Speech voice combo slot labels to reflect saved-state.
    void updateFishVoiceComboLabels();
    ProjectData captureProjectData() const;
    void applyProjectData(const ProjectData& data);
    void updateWindowTitle();
    void refreshVoicePresetCombo();
    int voiceIndexForShortName(tts::Provider provider, const QString& shortName) const;
    void setTimelineSegments(const std::vector<TimelineSegment>& segments);
    void rebuildAudioFromTimeline();
    void refreshRecentProjectsMenu();
    void captureRemoteGpuState();
    void playPreviewSamples(const std::vector<float>& samples, int sampleRate);

    // Runs on a background thread (QtConcurrent). Lazily loads Kokoro/Piper
    // engines + the shared espeak-ng phonemizer on first use.
    SynthOutput runSynthesis(const SynthRequest& req);

    // Synthesizes a single (provider, voice, text) segment with no mixing.
    // Shared by single-speaker synthesis and multi-speaker dialogue lines.
    tts::AudioBuffer synthesizeSegment(tts::Provider provider, const tts::VoiceEntry& voice,
                                        const std::string& text, float speed);

    // Synthesizes a single segment, optionally blending voiceA/voiceB
    // (pctA/100 .. (100-pctA)/100). Falls back to synthesizeSegment when
    // mixing is disabled or unsupported by the provider.
    tts::AudioBuffer synthesizeSegmentMixed(tts::Provider provider, const tts::VoiceEntry& voiceA,
                                             bool mixEnabled, const tts::VoiceEntry& voiceB, int pctA,
                                             const std::string& text, float speed);

    // Renders each dialogue segment, resamples to a common rate, and
    // concatenates with silence gaps between lines.
    SynthOutput runMultiSynthesis(const MultiSynthRequest& req);

    // Runs DeepFilterNet denoising on `input` and returns an 80% wet / 20%
    // dry blend at the same sample rate and length. Lazily loads the DFN
    // engine on first use. Runs on a background thread (QtConcurrent).
    tts::AudioBuffer runEnhance(const tts::AudioBuffer& input);

    // Loads an audio file into a mono float buffer. WAV files are parsed
    // directly; other formats (mp3, flac, ...) are decoded via QAudioDecoder.
    bool decodeAudioFile(const QString& path, tts::AudioBuffer* out);

    QString modelDir_;       // .../models/supertonic/onnx
    QString voiceStylesDir_; // .../models/supertonic/voice_styles
    QString kokoroModelDir_; // .../models/kokoro
    QString kokoroDeModelDir_; // .../models/kokoro_de_martin (German "martin" voice)
    QString kokoroDeVictoriaModelDir_; // .../models/kokoro_de_victoria (German "victoria" voice)
    QString piperModelDir_;  // .../models/piper
    QString espeakDataDir_;  // app dir (parent of espeak-ng-data)
    QString dfnModelDir_;    // .../models/deepfilternet

    QCheckBox* gpuCheckBox_;
    RemoteGpuPanel* remoteGpuPanel_ = nullptr;
    QPushButton* settingsBtn_;
    TagInserter* tagInserter_ = nullptr;
    ProviderHintsPanel* providerHints_ = nullptr;
    QPushButton* previewBtn_ = nullptr;
    QPushButton* voicePreviewBtn_ = nullptr;
    QPushButton* importTextBtn_ = nullptr;
    QPushButton* cancelSynthBtn_ = nullptr;
    TextStatsPanel* textStats_ = nullptr;
    QMenu* recentProjectsMenu_ = nullptr;
    QComboBox* voicePresetCombo_ = nullptr;
    QPushButton* saveVoicePresetBtn_ = nullptr;
    QPushButton* deleteVoicePresetBtn_ = nullptr;
    QTabWidget* tabWidget_ = nullptr;
    TimelinePanel* timelinePanel_ = nullptr;
    PronunciationPanel* pronunciationPanel_ = nullptr;

    QString currentProjectPath_;
    std::vector<VoicePreset> voicePresets_;
    bool projectDirty_ = false;
    std::vector<tts::PronunciationEntry> pronunciationDictionary_;

    // Fish Audio S2 Pro panel (shown only when FishSpeech provider is active)
    QWidget*     fishPanel_;
    QLabel*      fishRefAudioLabel_;
    QPushButton* fishBrowseBtn_;
    QPushButton* fishClearBtn_;
    QComboBox*   fishSlotCombo_;
    QPushButton* fishSaveSlotBtn_;
    QPushButton* fishExtractTokensBtn_;

    QPlainTextEdit* textEdit_;
    QComboBox* providerCombo_;
    QWidget* voiceFilterWidget_ = nullptr; // gender+language filters (Edge only)
    QComboBox* voiceGenderFilter_ = nullptr;
    QComboBox* voiceLangFilter_ = nullptr;
    QComboBox* voiceCombo_;
    QCheckBox* mixCheckBox_;
    QWidget* mixDetailWidget_ = nullptr;
    QComboBox* voiceBCombo_;
    QSlider* mixSlider_;
    QLabel* mixLabel_;
    QSlider* speedSlider_;
    QLabel* speedLabel_;
    QCheckBox* humanizerCheckBox_;
    QPushButton* humanizerSettingsBtn_ = nullptr; // "⚙" opens per-stage settings
    QCheckBox* streamPlaybackCheckBox_ = nullptr;
    EqPanel* eqPanel_;
    QDialog* eqDialog_;          // persistent non-modal EQ window
    QPushButton* eqDialogBtn_;   // "EQ..." button beside Settings
    QPushButton* synthesizeBtn_;
    QPushButton* playBtn_;
    QPushButton* pauseBtn_;
    QPushButton* stopBtn_;
    QPushButton* exportBtn_;
    QPushButton* enhanceBtn_;
    QLabel* statusBarDuration_ = nullptr;
    QLabel* statusBarDirty_ = nullptr;
    QLabel* statusBarGpu_ = nullptr;
    QLabel* statusBarRemote_ = nullptr;
    QProgressBar* progressBar_;

    // Editable chunking/gap settings (see onSettingsClicked), persisted via QSettings.
    int maxChunkChars_ = 400;
    int sentenceGapMs_ = 150;
    int paragraphGapMs_ = 600;

    // Whether to apply the Natural Humanizer (SSML + DSP) to Microsoft Edge
    // TTS output; captured on the UI thread before dispatching synthesis.
    bool humanizerEnabled_ = false;

    // Per-stage Natural Humanizer configuration (edited via the "⚙" dialog,
    // persisted in QSettings). Used by the worker when humanizerEnabled_ is set.
    tts::HumanizerSettings humanizerSettings_;

    std::shared_ptr<tts::SupertonicEngine> supertonicEngine_;
    std::shared_ptr<tts::KokoroEngine> kokoroEngine_;
    std::shared_ptr<tts::KokoroEngine> kokoroDeEngine_; // German "martin" voice (separate model)
    std::shared_ptr<tts::KokoroEngine> kokoroDeVictoriaEngine_; // German "victoria" voice (separate model)
    std::shared_ptr<tts::Phonemizer> phonemizer_;
    std::map<std::string, std::shared_ptr<tts::PiperEngine>> piperEngines_;
    std::shared_ptr<tts::EdgeTtsEngine> edgeTtsEngine_;
    std::shared_ptr<tts::DeepFilterNetEngine> dfnEngine_;

    // Remote Kaggle GPU Kokoro state, captured on the UI thread right before
    // dispatching synthesis to a background thread.
    bool remoteKokoroEnabled_ = false;
    QString remoteKokoroUrl_;
    std::shared_ptr<tts::RemoteKokoroEngine> remoteKokoroEngine_;

    // Fish Audio S2 Pro remote state
    bool    remoteFishEnabled_  = false;
    QString remoteFishUrl_;
    QString fishRefAudioPath_;  // path of WAV loaded via Browse (empty = random)
    std::shared_ptr<tts::RemoteFishSpeechEngine> remoteFishEngine_;

    QFutureWatcher<std::shared_ptr<tts::SupertonicEngine>>* engineWatcher_;
    QFutureWatcher<SynthOutput>* synthWatcher_;
    QFutureWatcher<SynthOutput>* multiSynthWatcher_;
    QFutureWatcher<tts::AudioBuffer>* enhanceWatcher_;
    QFutureWatcher<bool>* stopKaggleWatcher_;
    QFutureWatcher<bool>* exportWatcher_;
    QFutureWatcher<QString>* fishTokenWatcher_;  // async VQ token extraction
    QFutureWatcher<bool>*    stopFishWatcher_;
    QFutureWatcher<SynthOutput>* previewWatcher_ = nullptr;
    QFutureWatcher<bool>* batchExportWatcher_ = nullptr;
    QFutureWatcher<tts::AudioBuffer>* segmentRerenderWatcher_ = nullptr;
    QFutureWatcher<tts::EdgeTtsEngine::ConnectionTestResult>* edgeTestWatcher_ = nullptr;
    QString pendingExportPath_;
    QString pendingBatchError_;
    std::shared_ptr<QString> pendingBatchErrorPtr_;
    int pendingBatchItemIndex_ = -1;
    int pendingRerenderSegmentIndex_ = -1;
    bool edgeTestHandled_ = false;
    bool streamingPlayback_ = false;
    bool partialPlaybackStarted_ = false;
    std::vector<float> partialPlaybackSamples_;
    int partialPlaybackSampleRate_ = 44100;

    MultiSpeakerPanel* multiPanel_;
    BatchQueueDialog* batchQueueDialog_ = nullptr;
    QTimer* playbackTimer_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    double playbackStartOffsetSec_ = 0.0;
    QPushButton* previewDisableBtn_ = nullptr;
    std::atomic<bool> synthesisCancel_{false};

    tts::AudioBuffer rawAudio_;
    std::vector<float> processedAudio_;

    QAudioSink* audioSink_ = nullptr;
    QByteArray playbackBytes_;
    QBuffer playbackBuffer_;
};
