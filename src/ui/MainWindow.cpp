#include "MainWindow.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <future>
#include <thread>

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QAudioSink>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QMetaObject>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QException>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QShortcut>
#include <QStatusBar>
#include <QTextDocument>
#include <QTimer>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "../core/GpuDevice.h"
#include "../core/ProviderValidation.h"
#include "../core/TextMarkup.h"
#include "../dsp/GraphicEq.h"
#include "../dsp/Humanizer.h"
#include "AudioTimelineBuilder.h"
#include "CaptionExporter.h"
#include "../dsp/Normalizer.h"
#include "../dsp/Resampler.h"
#include "../dsp/WavReader.h"
#include "../dsp/WavWriter.h"
#include "ComboUtils.h"
#include "EqPanel.h"
#include "BatchQueueDialog.h"
#include "FirstRunWizard.h"
#include "ModelManagerDialog.h"
#include "ProviderHintsPanel.h"
#include "ReadMeDialog.h"
#include "RecentProjects.h"
#include "RemoteGpuPanel.h"
#include "TagInserter.h"
#include "TextStatsPanel.h"
#include "ThemeManager.h"
#include "../dsp/AudioExporter.h"

namespace {

// Resolves the full-provider-list index for a voice combo's current selection.
// populateVoiceCombos() stores that index in each item's user data; falls back
// to the positional index for combos populated elsewhere (e.g. Fish slots).
int comboVoiceFullIndex(const QComboBox* combo) {
    if (combo == nullptr || combo->currentIndex() < 0) {
        return -1;
    }
    const QVariant d = combo->currentData();
    return d.isValid() ? d.toInt() : combo->currentIndex();
}

// QtConcurrent wraps any exception not derived from QException into
// QUnhandledException, whose what() is just "Unknown exception". Rethrowing
// as this type preserves the original error message across the worker thread.
class SynthesisException : public QException {
public:
    explicit SynthesisException(std::string message) : message_(std::move(message)) {}
    const char* what() const noexcept override { return message_.c_str(); }
    void raise() const override { throw *this; }
    QException* clone() const override { return new SynthesisException(*this); }

private:
    std::string message_;
};

constexpr int kDefaultTotalSteps = 8;

std::string makeSegmentLabel(const std::string& text) {
    if (text.size() <= 48) {
        return text;
    }
    return text.substr(0, 45) + "...";
}

std::string trimCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

bool isBlank(const std::string& text) {
    return text.empty() ||
           std::all_of(text.begin(), text.end(),
                       [](unsigned char c) { return std::isspace(c) != 0; });
}

bool isUtf8ContinuationByte(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

// Splits text into chunks of at most maxChars bytes, preferring to break
// after sentence-ending punctuation, then whitespace, and only falling back
// to a hard split (never inside a UTF-8 multi-byte sequence) as a last
// resort.
std::vector<std::string> splitTextIntoChunks(const std::string& text, size_t maxChars) {
    std::vector<std::string> chunks;
    size_t pos = 0;
    const size_t n = text.size();
    while (pos < n) {
        while (pos < n && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= n) break;

        if (n - pos <= maxChars) {
            chunks.push_back(text.substr(pos));
            break;
        }

        const size_t limit = pos + maxChars;
        size_t splitAt = std::string::npos;

        // Prefer the last sentence-ending punctuation within the window.
        for (size_t i = limit; i > pos; --i) {
            char c = text[i - 1];
            if (c == '.' || c == '!' || c == '?' || c == '\n') {
                splitAt = i;
                break;
            }
        }
        // Fall back to the last whitespace within the window.
        if (splitAt == std::string::npos) {
            for (size_t i = limit; i > pos; --i) {
                if (std::isspace(static_cast<unsigned char>(text[i - 1]))) {
                    splitAt = i;
                    break;
                }
            }
        }
        // Last resort: hard split at the window edge, backing off any
        // leading UTF-8 continuation bytes so we never cut mid-character.
        if (splitAt == std::string::npos) {
            splitAt = limit;
            while (splitAt > pos && isUtf8ContinuationByte(static_cast<unsigned char>(text[splitAt]))) {
                --splitAt;
            }
            if (splitAt == pos) {
                splitAt = limit;
            }
        }

        chunks.push_back(text.substr(pos, splitAt - pos));
        pos = splitAt;
    }
    return chunks;
}

// Splits text on blank lines into paragraphs.
std::vector<std::string> splitIntoParagraphs(const std::string& text) {
    std::vector<std::string> paragraphs;
    std::string current;
    size_t pos = 0;
    while (true) {
        size_t nl = text.find('\n', pos);
        std::string line = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        const bool blank = std::all_of(line.begin(), line.end(),
                                        [](unsigned char c) { return std::isspace(c); });
        if (blank) {
            if (!current.empty()) {
                paragraphs.push_back(current);
                current.clear();
            }
        } else {
            if (!current.empty()) current += "\n";
            current += line;
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    if (!current.empty()) {
        paragraphs.push_back(current);
    }
    return paragraphs;
}

// Splits text into paragraphs, then each paragraph into chunks of at most
// maxChars (via splitTextIntoChunks). `startsNewParagraph[i]` is true for the
// first chunk of every paragraph after the first, so the caller can insert a
// paragraph-length silence gap there instead of a sentence-length one.
void splitTextIntoChunksWithParagraphs(const std::string& text, size_t maxChars,
                                        std::vector<std::string>* chunks,
                                        std::vector<bool>* startsNewParagraph) {
    chunks->clear();
    startsNewParagraph->clear();

    std::vector<std::string> paragraphs = splitIntoParagraphs(text);
    if (paragraphs.empty()) {
        paragraphs.push_back(text);
    }
    for (size_t p = 0; p < paragraphs.size(); ++p) {
        std::vector<std::string> pieces = splitTextIntoChunks(paragraphs[p], maxChars);
        if (pieces.empty()) {
            pieces.push_back(paragraphs[p]);
        }
        for (size_t i = 0; i < pieces.size(); ++i) {
            chunks->push_back(pieces[i]);
            startsNewParagraph->push_back(p > 0 && i == 0);
        }
    }
}

struct ChunkPlan {
    std::vector<std::string> chunks;
    std::vector<std::string> chunkSpeakable;
    std::vector<int> chunkPauseAfterMs;
};

ChunkPlan buildChunkPlan(const std::string& text, tts::Provider provider,
                         const std::vector<tts::PronunciationEntry>& dictionary, int maxChunkChars,
                         int sentenceGapMs, int paragraphGapMs) {
    ChunkPlan plan;
    const std::vector<tts::SpeechPart> speechParts = tts::prepareSpeechParts(text, dictionary);
    int carryPauseMs = 0;
    for (size_t p = 0; p < speechParts.size(); ++p) {
        std::string partText =
            provider == tts::Provider::EdgeTts ? speechParts[p].text : tts::stripMarkup(speechParts[p].text);
        partText = trimCopy(partText);
        if (isBlank(partText)) {
            carryPauseMs += speechParts[p].pauseAfterMs;
            continue;
        }

        std::vector<std::string> partChunks;
        std::vector<bool> startsNewParagraph;
        splitTextIntoChunksWithParagraphs(partText, static_cast<size_t>(maxChunkChars), &partChunks,
                                          &startsNewParagraph);
        if (partChunks.empty()) {
            partChunks.push_back(partText);
            startsNewParagraph.push_back(false);
        }

        for (size_t c = 0; c < partChunks.size(); ++c) {
            const std::string trimmedChunk = trimCopy(partChunks[c]);
            if (isBlank(trimmedChunk)) {
                continue;
            }
            if (!plan.chunks.empty() && carryPauseMs > 0) {
                plan.chunkPauseAfterMs.back() += carryPauseMs;
                carryPauseMs = 0;
            }
            plan.chunks.push_back(provider == tts::Provider::EdgeTts ? partChunks[c] : trimmedChunk);
            plan.chunkSpeakable.push_back(provider == tts::Provider::EdgeTts ? tts::stripMarkup(partChunks[c])
                                                                             : trimmedChunk);
            int pauseMs = 0;
            if (c + 1 < partChunks.size()) {
                pauseMs = startsNewParagraph[c + 1] ? paragraphGapMs : sentenceGapMs;
            } else {
                pauseMs = speechParts[p].pauseAfterMs;
            }
            plan.chunkPauseAfterMs.push_back(pauseMs);
        }
    }

    if (!plan.chunks.empty() && carryPauseMs > 0) {
        plan.chunkPauseAfterMs.back() += carryPauseMs;
    }
    if (plan.chunks.empty()) {
        const std::string fallback = trimCopy(tts::stripMarkup(text));
        plan.chunks.push_back(fallback.empty() ? text : fallback);
        plan.chunkSpeakable.push_back(plan.chunks.back());
        plan.chunkPauseAfterMs.push_back(0);
    }
    return plan;
}

// Synthesizes each chunk via `synthesizeChunk` and concatenates the results
// in order, inserting `gapSamplesBefore[i]` zero samples before chunk i's
// audio (used for sentence/paragraph pauses). The first chunk runs
// synchronously, since it lazily constructs whatever engine/phonemizer state
// the rest of the chunks depend on (their lazy-init is then just a read of an
// already-set shared_ptr, which is safe to do concurrently). The remaining
// chunks run on a bounded number of worker threads; `onChunkDone(i)` is
// invoked in order after chunk i finishes.
std::vector<float> synthesizeChunksConcurrently(
    const std::vector<std::string>& chunks, const std::vector<int>& gapSamplesBefore,
    const std::function<std::vector<float>(const std::string&)>& synthesizeChunk,
    const std::function<void(size_t)>& onChunkDone) {
    std::vector<std::vector<float>> results(chunks.size());

    results[0] = synthesizeChunk(chunks[0]);
    onChunkDone(0);

    const size_t concurrency =
        std::max<size_t>(1, std::thread::hardware_concurrency() / 2);

    size_t i = 1;
    while (i < chunks.size()) {
        const size_t batchEnd = std::min(chunks.size(), i + concurrency);
        std::vector<std::future<std::vector<float>>> futures;
        for (size_t j = i; j < batchEnd; ++j) {
            futures.push_back(std::async(std::launch::async, synthesizeChunk, chunks[j]));
        }
        for (size_t j = i; j < batchEnd; ++j) {
            results[j] = futures[j - i].get();
            onChunkDone(j);
        }
        i = batchEnd;
    }

    std::vector<float> out;
    for (size_t j = 0; j < results.size(); ++j) {
        if (j < gapSamplesBefore.size() && gapSamplesBefore[j] > 0) {
            out.insert(out.end(), static_cast<size_t>(gapSamplesBefore[j]), 0.0f);
        }
        out.insert(out.end(), results[j].begin(), results[j].end());
    }
    return out;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    const QString appDir = QCoreApplication::applicationDirPath();
    modelDir_ = appDir + "/models/supertonic/onnx";
    voiceStylesDir_ = appDir + "/models/supertonic/voice_styles";
    kokoroModelDir_ = appDir + "/models/kokoro";
    kokoroDeModelDir_ = appDir + "/models/kokoro_de_martin";
    kokoroDeVictoriaModelDir_ = appDir + "/models/kokoro_de_victoria";
    piperModelDir_ = appDir + "/models/piper";
    espeakDataDir_ = appDir;
    dfnModelDir_ = appDir + "/models/deepfilternet";

    setWindowTitle("EdgeTTS-Studio Native");

    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&New Project", this, &MainWindow::onNewProject);
    fileMenu->addAction("&Open Project...", this, &MainWindow::onOpenProject);
    fileMenu->addAction("&Save Project", this, &MainWindow::onSaveProject);
    fileMenu->addAction("Save Project &As...", this, &MainWindow::onSaveProjectAs);
    fileMenu->addSeparator();
    fileMenu->addAction("Export &Package...", this, &MainWindow::onExportPackage);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* readMeMenu = menuBar()->addMenu("&Read me");
    readMeMenu->addAction("Text Tags && Examples...", this, [this]() {
        ReadMeDialog dialog(this);
        dialog.exec();
    });

    auto* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("Model &Manager...", this, &MainWindow::onModelManagerClicked);
    toolsMenu->addAction("&Batch / Chapter Queue...", this, &MainWindow::onBatchQueueClicked);
    toolsMenu->addSeparator();
    toolsMenu->addAction("Toggle &Dark Mode", this, &MainWindow::onToggleDarkMode);

    recentProjectsMenu_ = fileMenu->addMenu("Recent Projects");
    refreshRecentProjectsMenu();

    auto* central = new QWidget(this);
    auto* columns = new QHBoxLayout(central);
    columns->setContentsMargins(6, 2, 6, 2);
    columns->setSpacing(8);

    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(3);
    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(3);

    auto* textHeaderRow = new QHBoxLayout();
    textHeaderRow->addWidget(new QLabel("Text to synthesize:", central));
    textHeaderRow->addStretch(1);
    importTextBtn_ = new QPushButton("Import Text...", central);
    importTextBtn_->setToolTip("Load a .txt file into the editor");
    previewBtn_ = new QPushButton("Preview Selection", central);
    previewBtn_->setToolTip("Synthesize and play the selected text (Ctrl+Shift+P)");
    textHeaderRow->addWidget(importTextBtn_);
    textHeaderRow->addWidget(previewBtn_);
    leftCol->addLayout(textHeaderRow);

    textEdit_ = new QPlainTextEdit(central);
    textEdit_->setPlainText("This is a test of the native TTS engine.");
    textEdit_->setMinimumHeight(80);
    leftCol->addWidget(textEdit_, 1);

    textStats_ = new TextStatsPanel(central);
    leftCol->addWidget(textStats_);

    tagInserter_ = new TagInserter(textEdit_, central);
    leftCol->addWidget(tagInserter_);

    providerHints_ = new ProviderHintsPanel(central);
    leftCol->addWidget(providerHints_);

    auto* providerRow = new QHBoxLayout();
    providerRow->addWidget(new QLabel("Provider:", central));
    providerCombo_ = new QComboBox(central);
    providerCombo_->addItem("Supertonic (Multilingual)");
    providerCombo_->addItem("Kokoro (Local Neural)");
    providerCombo_->addItem("Piper (German)");
    providerCombo_->addItem("Microsoft Edge (Online)");
    providerCombo_->addItem("Fish Audio S2 (Kaggle)");
    tts::applyNeatComboPopup(providerCombo_, /*searchable=*/false);
    providerRow->addWidget(providerCombo_);
    humanizerCheckBox_ = new QCheckBox("Natural Humanizer (Edge TTS)", central);
    humanizerSettingsBtn_ = new QPushButton(QString::fromUtf8("\xE2\x9A\x99"), central); // gear
    humanizerSettingsBtn_->setToolTip("Customize the Natural Humanizer stages (EQ, compression, reverb, de-esser, loudness)");
    humanizerSettingsBtn_->setFixedWidth(30);
    streamPlaybackCheckBox_ = new QCheckBox("Play while synthesizing", central);
    streamPlaybackCheckBox_->setToolTip("Start playback after the first chunk finishes rendering");
    providerRow->addWidget(humanizerCheckBox_);
    providerRow->addWidget(humanizerSettingsBtn_);
    providerRow->addWidget(streamPlaybackCheckBox_);
    connect(humanizerSettingsBtn_, &QPushButton::clicked, this, &MainWindow::onHumanizerSettingsClicked);
    providerRow->addStretch(1);
    rightCol->addLayout(providerRow);

    auto* voiceRow = new QHBoxLayout();
    voiceRow->addWidget(new QLabel("Voice:", central));

    // Gender + language filters (Edge only) — collapsed into one widget so the
    // whole group can be shown/hidden together.
    voiceFilterWidget_ = new QWidget(central);
    auto* filterRow = new QHBoxLayout(voiceFilterWidget_);
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(4);
    voiceGenderFilter_ = new QComboBox(voiceFilterWidget_);
    voiceGenderFilter_->addItems({"All genders", "Male", "Female"});
    voiceGenderFilter_->setToolTip("Filter Edge voices by gender");
    voiceLangFilter_ = new QComboBox(voiceFilterWidget_);
    voiceLangFilter_->addItem("All languages");
    for (const auto& loc : tts::edgeTtsLocales()) {
        voiceLangFilter_->addItem(QString::fromStdString(loc));
    }
    voiceLangFilter_->setToolTip("Filter Edge voices by language/locale");
    tts::applyNeatComboPopup(voiceGenderFilter_, /*searchable=*/false);
    tts::applyNeatComboPopup(voiceLangFilter_, /*searchable=*/true); // long locale list
    filterRow->addWidget(voiceGenderFilter_);
    filterRow->addWidget(voiceLangFilter_);
    voiceRow->addWidget(voiceFilterWidget_);

    voiceCombo_ = new QComboBox(central);
    voiceRow->addWidget(voiceCombo_, 1);

    // Re-filter the voice list (preserving the current selection if possible).
    auto reFilter = [this]() {
        const int keepFull = (voiceCombo_->currentIndex() >= 0 && voiceCombo_->currentData().isValid())
                                 ? voiceCombo_->currentData().toInt()
                                 : -1;
        populateVoiceCombos();
        if (keepFull >= 0 && voiceCombo_->findData(keepFull) >= 0) {
            voiceCombo_->setCurrentIndex(voiceCombo_->findData(keepFull));
        }
    };
    connect(voiceGenderFilter_, &QComboBox::currentIndexChanged, this, [reFilter](int) { reFilter(); });
    connect(voiceLangFilter_, &QComboBox::currentIndexChanged, this, [reFilter](int) { reFilter(); });
    voicePreviewBtn_ = new QPushButton("Preview Voice", central);
    voicePreviewBtn_->setToolTip("Play a short sample with the selected voice");
    voiceRow->addWidget(voicePreviewBtn_);
    mixCheckBox_ = new QCheckBox("Mix", central);
    mixCheckBox_->setToolTip("Blend two voices (Supertonic / Kokoro only)");
    voiceRow->addWidget(mixCheckBox_);
    rightCol->addLayout(voiceRow);

    mixDetailWidget_ = new QWidget(central);
    auto* mixRow = new QHBoxLayout(mixDetailWidget_);
    mixRow->setContentsMargins(16, 0, 0, 0);
    mixRow->addWidget(new QLabel("Voice B:", mixDetailWidget_));
    voiceBCombo_ = new QComboBox(mixDetailWidget_);
    mixRow->addWidget(voiceBCombo_, 1);
    mixSlider_ = new QSlider(Qt::Horizontal, mixDetailWidget_);
    mixSlider_->setRange(0, 100);
    mixSlider_->setValue(50);
    mixSlider_->setMaximumWidth(150);
    mixRow->addWidget(mixSlider_);
    mixLabel_ = new QLabel("A 50% / B 50%", mixDetailWidget_);
    mixRow->addWidget(mixLabel_);
    rightCol->addWidget(mixDetailWidget_);
    mixDetailWidget_->setVisible(false);
    connect(mixSlider_, &QSlider::valueChanged, this, [this](int v) {
        mixLabel_->setText(QString("A %1% / B %2%").arg(v).arg(100 - v));
    });
    connect(mixCheckBox_, &QCheckBox::toggled, this, &MainWindow::updateMixUiEnabled);

    auto* speedRow = new QHBoxLayout();
    speedRow->addWidget(new QLabel("Speed:", central));
    speedSlider_ = new QSlider(Qt::Horizontal, central);
    speedSlider_->setRange(70, 200); // 0.70x .. 2.00x
    speedSlider_->setValue(105);
    speedRow->addWidget(speedSlider_);
    speedLabel_ = new QLabel("1.05x", central);
    speedRow->addWidget(speedLabel_);
    rightCol->addLayout(speedRow);
    connect(speedSlider_, &QSlider::valueChanged, this, [this](int v) {
        speedLabel_->setText(QString::number(v / 100.0, 'f', 2) + "x");
    });

    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel("Voice Preset:", central));
    voicePresetCombo_ = new QComboBox(central);
    voicePresetCombo_->setMinimumWidth(180);
    tts::applyNeatComboPopup(voicePresetCombo_, /*searchable=*/false);
    presetRow->addWidget(voicePresetCombo_, 1);
    saveVoicePresetBtn_ = new QPushButton("Save Preset...", central);
    deleteVoicePresetBtn_ = new QPushButton("Delete", central);
    presetRow->addWidget(saveVoicePresetBtn_);
    presetRow->addWidget(deleteVoicePresetBtn_);
    rightCol->addLayout(presetRow);

    voicePresets_ = ProjectManager::loadVoicePresets();
    refreshVoicePresetCombo();
    connect(voicePresetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onVoicePresetChanged);
    connect(saveVoicePresetBtn_, &QPushButton::clicked, this, &MainWindow::onSaveVoicePreset);
    connect(deleteVoicePresetBtn_, &QPushButton::clicked, this, &MainWindow::onDeleteVoicePreset);

    // --- Fish Audio S2 Pro panel (visible only when FishSpeech provider selected) ---
    fishPanel_ = new QGroupBox("Fish Audio S2 — Voice Cloning", central);
    auto* fishLayout = new QVBoxLayout(fishPanel_);
    fishLayout->setSpacing(4);

    // Row 1: reference audio file browse
    auto* fishRefRow = new QHBoxLayout();
    fishRefRow->addWidget(new QLabel("Ref. Audio:", fishPanel_));
    fishRefAudioLabel_ = new QLabel("None  (random voice)", fishPanel_);
    fishRefAudioLabel_->setStyleSheet("color: gray; font-style: italic;");
    fishRefAudioLabel_->setToolTip(
        "Select a WAV/MP3 (10-30 s) of the voice to clone.\n"
        "The file is sent to your Kaggle server with each synthesis request.\n"
        "Save it to a Slot to reuse it across sessions without re-browsing.");
    fishRefRow->addWidget(fishRefAudioLabel_, 1);
    fishBrowseBtn_ = new QPushButton("Browse...", fishPanel_);
    fishBrowseBtn_->setToolTip("Load a local WAV/MP3 as the reference voice");
    fishClearBtn_  = new QPushButton("Clear", fishPanel_);
    fishClearBtn_->setToolTip("Remove the reference audio and use random voice mode");
    fishClearBtn_->setEnabled(false);
    fishRefRow->addWidget(fishBrowseBtn_);
    fishRefRow->addWidget(fishClearBtn_);
    fishLayout->addLayout(fishRefRow);

    // Row 2: save to slot + extract tokens
    auto* fishSlotRow = new QHBoxLayout();
    fishSlotRow->addWidget(new QLabel("Save to:", fishPanel_));
    fishSlotCombo_ = new QComboBox(fishPanel_);
    fishSlotCombo_->addItem("Slot 1");
    fishSlotCombo_->addItem("Slot 2");
    fishSlotCombo_->addItem("Slot 3");
    fishSlotCombo_->setToolTip("Choose which persistent voice slot to save the reference audio into.\n"
                               "Saved slots appear in the Voice combo above as ✓ (saved).\n"
                               "The WAV is stored on this PC in AppData/EdgeTTS-Studio/fish_voices/");
    fishSlotRow->addWidget(fishSlotCombo_);
    fishSaveSlotBtn_ = new QPushButton("Save Slot", fishPanel_);
    fishSaveSlotBtn_->setToolTip("Copy the loaded reference audio into the selected slot (stored locally)");
    fishExtractTokensBtn_ = new QPushButton("Extract Tokens", fishPanel_);
    fishExtractTokensBtn_->setToolTip(
        "Send the reference audio to the Kaggle server to extract VQ token codes.\n"
        "Tokens are saved locally as JSON alongside the slot WAV.\n"
        "Requires the Fish Audio Kaggle server to be running.");
    fishSlotRow->addWidget(fishSaveSlotBtn_);
    fishSlotRow->addWidget(fishExtractTokensBtn_);

    // Tags hint — compact inline label
    fishSlotRow->addSpacing(16);
    auto* fishTagsHint = new QLabel(
        "<span style='color:#666'>Tags: "
        "<tt>[laugh]</tt> <tt>[whisper]</tt> <tt>[cry]</tt> "
        "<tt>[happy]</tt> <tt>[sad]</tt> <tt>[angry]</tt></span>",
        fishPanel_);
    fishTagsHint->setToolTip("Type these tags directly inside the text box to control emotion/style");
    fishSlotRow->addWidget(fishTagsHint);
    fishSlotRow->addStretch(1);
    fishLayout->addLayout(fishSlotRow);

    fishPanel_->setVisible(false);
    rightCol->addWidget(fishPanel_);
    // -------------------------------------------------------------------------

    // EQ lives in a persistent non-modal dialog — opened via the "EQ..." button.
    // This keeps the main window compact regardless of which provider is active.
    eqDialog_ = new QDialog(this);
    eqDialog_->setWindowTitle("Equalizer");
    eqDialog_->setWindowFlags(eqDialog_->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    auto* eqDialogLayout = new QVBoxLayout(eqDialog_);
    eqPanel_ = new EqPanel(eqDialog_);
    eqDialogLayout->addWidget(eqPanel_);
    eqDialogLayout->setContentsMargins(6, 6, 6, 6);
    eqDialog_->resize(660, 240);
    connect(eqPanel_, &EqPanel::gainsChanged, this, &MainWindow::onEqChanged);

    auto* transportGroup = new QGroupBox("Transport", central);
    auto* transportRow = new QHBoxLayout(transportGroup);
    synthesizeBtn_ = new QPushButton("Synthesize", transportGroup);
    cancelSynthBtn_ = new QPushButton("Cancel", transportGroup);
    playBtn_ = new QPushButton("Play", transportGroup);
    pauseBtn_ = new QPushButton("Pause", transportGroup);
    stopBtn_ = new QPushButton("Stop", transportGroup);
    exportBtn_ = new QPushButton("Export Audio...", transportGroup);
    enhanceBtn_ = new QPushButton("Enhance (DeepFilterNet)...", transportGroup);
    playBtn_->setEnabled(false);
    pauseBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);
    exportBtn_->setEnabled(false);
    cancelSynthBtn_->setEnabled(false);
    synthesizeBtn_->setEnabled(false);
    transportRow->addWidget(synthesizeBtn_);
    transportRow->addWidget(cancelSynthBtn_);
    transportRow->addWidget(playBtn_);
    transportRow->addWidget(pauseBtn_);
    transportRow->addWidget(stopBtn_);
    transportRow->addWidget(exportBtn_);
    transportRow->addWidget(enhanceBtn_);
    rightCol->addWidget(transportGroup);

    progressBar_ = new QProgressBar(central);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    rightCol->addWidget(progressBar_);

    columns->addLayout(leftCol, 1);
    columns->addLayout(rightCol, 1);

    auto* outerWidget = new QWidget(this);
    auto* outerLayout = new QVBoxLayout(outerWidget);
    outerLayout->setContentsMargins(4, 4, 4, 4);
    outerLayout->setSpacing(2);

    auto* hwRow = new QHBoxLayout();
    gpuCheckBox_ = new QCheckBox("Use GPU acceleration (DirectML) if available", outerWidget);
    hwRow->addWidget(gpuCheckBox_);
    settingsBtn_ = new QPushButton("Settings...", outerWidget);
    eqDialogBtn_ = new QPushButton("EQ...", outerWidget);
    eqDialogBtn_->setToolTip("Open / close the 7-band graphic Equalizer");
    eqDialogBtn_->setCheckable(true);
    hwRow->addWidget(settingsBtn_);
    hwRow->addWidget(eqDialogBtn_);
    hwRow->addStretch(1);
    outerLayout->addLayout(hwRow);

    remoteGpuPanel_ = new RemoteGpuPanel(outerWidget);
    outerLayout->addWidget(remoteGpuPanel_);

    tabWidget_ = new QTabWidget(outerWidget);
    central->setMinimumSize(QSize(0, 0));
    tabWidget_->addTab(central, "Single Speaker");
    multiPanel_ = new MultiSpeakerPanel(this);
    multiPanel_->setMinimumSize(QSize(0, 0));
    tabWidget_->addTab(multiPanel_, "Multi-Speaker Dialogue");
    timelinePanel_ = new TimelinePanel(this);
    tabWidget_->addTab(timelinePanel_, "Timeline");
    pronunciationPanel_ = new PronunciationPanel(this);
    tabWidget_->addTab(pronunciationPanel_, "Pronunciation");
    outerLayout->addWidget(tabWidget_, 1);

    connect(timelinePanel_, &TimelinePanel::rebuildRequested, this, &MainWindow::onTimelineRebuildRequested);
    connect(timelinePanel_, &TimelinePanel::seekRequested, this, &MainWindow::seekPlayback);
    connect(pronunciationPanel_, &PronunciationPanel::previewRequested, this,
            &MainWindow::onPronunciationPreviewRequested);
    connect(pronunciationPanel_, &PronunciationPanel::dictionaryChanged, this, [this]() {
        markProjectDirty();
    });

    setCentralWidget(outerWidget);

    statusBarDuration_ = new QLabel(this);
    statusBarDirty_ = new QLabel(this);
    statusBarGpu_ = new QLabel(this);
    statusBarRemote_ = new QLabel(this);
    statusBar()->addPermanentWidget(statusBarDuration_);
    statusBar()->addPermanentWidget(statusBarDirty_);
    statusBar()->addPermanentWidget(statusBarGpu_);
    statusBar()->addPermanentWidget(statusBarRemote_);
    setStatus("Loading Supertonic models...");
    updateStatusBar();

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(120'000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::onAutosaveTick);
    autosaveTimer_->start();

    resize(980, 620);

    connect(multiPanel_, &MultiSpeakerPanel::renderRequested, this, &MainWindow::onMultiRenderRequested);
    connect(multiPanel_, &MultiSpeakerPanel::playRequested, this, &MainWindow::onPlayClicked);
    connect(multiPanel_, &MultiSpeakerPanel::pauseRequested, this, &MainWindow::onPauseClicked);
    connect(multiPanel_, &MultiSpeakerPanel::stopRequested, this, &MainWindow::onStopClicked);
    connect(multiPanel_, &MultiSpeakerPanel::exportRequested, this, &MainWindow::onExportClicked);
    connect(multiPanel_, &MultiSpeakerPanel::enhanceRequested, this, &MainWindow::onEnhanceClicked);
    connect(multiPanel_, &MultiSpeakerPanel::cancelRequested, this, &MainWindow::onCancelSynthesisClicked);
    connect(multiPanel_, &MultiSpeakerPanel::previewSelectionRequested, this,
            &MainWindow::onMultiPreviewSelectionRequested);
    connect(multiPanel_, &MultiSpeakerPanel::importTextRequested, this, &MainWindow::onMultiImportText);

    connect(providerCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::onProviderChanged);
    connect(synthesizeBtn_, &QPushButton::clicked, this, &MainWindow::onSynthesizeClicked);
    connect(playBtn_, &QPushButton::clicked, this, &MainWindow::onPlayClicked);
    connect(pauseBtn_, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(exportBtn_, &QPushButton::clicked, this, &MainWindow::onExportClicked);
    connect(enhanceBtn_, &QPushButton::clicked, this, &MainWindow::onEnhanceClicked);
    connect(cancelSynthBtn_, &QPushButton::clicked, this, &MainWindow::onCancelSynthesisClicked);
    connect(previewBtn_, &QPushButton::clicked, this, &MainWindow::onPreviewSelectionClicked);
    connect(voicePreviewBtn_, &QPushButton::clicked, this, &MainWindow::onVoicePreviewClicked);
    connect(importTextBtn_, &QPushButton::clicked, this, &MainWindow::onImportTextClicked);
    connect(settingsBtn_, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(eqDialogBtn_, &QPushButton::clicked, this, &MainWindow::onEqDialogClicked);
    connect(eqDialog_, &QDialog::finished, this, [this](int) {
        eqDialogBtn_->setChecked(false);
    });

    populateVoiceCombos();
    updateVoiceFilterUi();
    updateMixUiEnabled();

    // Apply the saved GPU/CPU preference (if any) before the Supertonic
    // engine load below picks an execution provider.
    {
        QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
        const bool useGpu = settings.value("useGpu", true).toBool();
        tts::setGpuPreferred(useGpu);
        gpuCheckBox_->setChecked(useGpu);

        remoteGpuPanel_->loadFromSettings();

        maxChunkChars_ = settings.value("maxChunkChars", 400).toInt();
        sentenceGapMs_ = settings.value("sentenceGapMs", 150).toInt();
        paragraphGapMs_ = settings.value("paragraphGapMs", 600).toInt();

        humanizerCheckBox_->setChecked(settings.value("edgeHumanizer", false).toBool());

        // Per-stage Natural Humanizer config (defaults come from the struct).
        const tts::HumanizerSettings d;
        auto& h = humanizerSettings_;
        h.eqEnabled       = settings.value("hum/eqEnabled", d.eqEnabled).toBool();
        h.eqWarmthDb      = settings.value("hum/eqWarmthDb", d.eqWarmthDb).toFloat();
        h.eqMidCutDb      = settings.value("hum/eqMidCutDb", d.eqMidCutDb).toFloat();
        h.eqAirDb         = settings.value("hum/eqAirDb", d.eqAirDb).toFloat();
        h.compEnabled     = settings.value("hum/compEnabled", d.compEnabled).toBool();
        h.compThresholdDb = settings.value("hum/compThresholdDb", d.compThresholdDb).toFloat();
        h.compRatio       = settings.value("hum/compRatio", d.compRatio).toFloat();
        h.compMakeupDb    = settings.value("hum/compMakeupDb", d.compMakeupDb).toFloat();
        h.reverbEnabled   = settings.value("hum/reverbEnabled", d.reverbEnabled).toBool();
        h.reverbWet       = settings.value("hum/reverbWet", d.reverbWet).toFloat();
        h.deEsserEnabled  = settings.value("hum/deEsserEnabled", d.deEsserEnabled).toBool();
        h.deEsserThreshDb = settings.value("hum/deEsserThreshDb", d.deEsserThreshDb).toFloat();
        h.deEsserRatio    = settings.value("hum/deEsserRatio", d.deEsserRatio).toFloat();
        h.loudnessEnabled = settings.value("hum/loudnessEnabled", d.loudnessEnabled).toBool();
        h.loudnessTargetDb = settings.value("hum/loudnessTargetDb", d.loudnessTargetDb).toFloat();
        h.ceilingEnabled  = settings.value("hum/ceilingEnabled", d.ceilingEnabled).toBool();
        h.ceilingDb       = settings.value("hum/ceilingDb", d.ceilingDb).toFloat();
    }
    connect(humanizerCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
        settings.setValue("edgeHumanizer", checked);
    });
    {
        const bool isEdge = static_cast<tts::Provider>(providerCombo_->currentIndex()) ==
                            tts::Provider::EdgeTts;
        humanizerCheckBox_->setEnabled(isEdge);
        humanizerSettingsBtn_->setEnabled(isEdge);
    }
    connect(gpuCheckBox_, &QCheckBox::toggled, this, &MainWindow::onGpuToggled);
    connect(remoteGpuPanel_, &RemoteGpuPanel::settingsChanged, this, [this]() {
        remoteGpuPanel_->saveToSettings();
    });
    connect(remoteGpuPanel_, &RemoteGpuPanel::kokoroToggled, this, &MainWindow::onRemoteKokoroToggled);
    connect(remoteGpuPanel_, &RemoteGpuPanel::stopKokoroRequested, this, &MainWindow::onStopKaggleClicked);
    connect(remoteGpuPanel_, &RemoteGpuPanel::stopFishRequested, this, &MainWindow::onStopFishKaggleClicked);
    connect(fishBrowseBtn_, &QPushButton::clicked, this, &MainWindow::onFishBrowseClicked);
    connect(fishClearBtn_,          &QPushButton::clicked, this, &MainWindow::onFishClearClicked);
    connect(fishSaveSlotBtn_,       &QPushButton::clicked, this, &MainWindow::onFishSaveSlotClicked);
    connect(fishExtractTokensBtn_,  &QPushButton::clicked, this, &MainWindow::onFishExtractTokensClicked);
    fishTokenWatcher_ = new QFutureWatcher<QString>(this);
    connect(fishTokenWatcher_, &QFutureWatcher<QString>::finished,
            this, &MainWindow::onFishExtractTokensFinished);

    stopFishWatcher_ = new QFutureWatcher<bool>(this);
    connect(stopFishWatcher_, &QFutureWatcher<bool>::finished,
            this, &MainWindow::onStopFishKaggleFinished);

    // Load the 4 Supertonic ONNX sessions on a background thread
    // (vector_estimator.onnx alone is ~244MB, so this takes a noticeable
    // moment). Kokoro/Piper engines + the espeak-ng phonemizer are loaded
    // lazily, on first use, inside runSynthesis().
    const std::string modelDirStd = modelDir_.toStdString();
    engineWatcher_ = new QFutureWatcher<std::shared_ptr<tts::SupertonicEngine>>(this);
    connect(engineWatcher_, &QFutureWatcher<std::shared_ptr<tts::SupertonicEngine>>::finished, this,
            &MainWindow::onEngineLoaded);
    auto future = QtConcurrent::run([modelDirStd]() -> std::shared_ptr<tts::SupertonicEngine> {
        return std::make_shared<tts::SupertonicEngine>(modelDirStd);
    });
    engineWatcher_->setFuture(future);

    synthWatcher_ = new QFutureWatcher<SynthOutput>(this);
    connect(synthWatcher_, &QFutureWatcher<SynthOutput>::finished, this, &MainWindow::onSynthesisFinished);

    multiSynthWatcher_ = new QFutureWatcher<SynthOutput>(this);
    connect(multiSynthWatcher_, &QFutureWatcher<SynthOutput>::finished, this,
            &MainWindow::onMultiSynthesisFinished);

    enhanceWatcher_ = new QFutureWatcher<tts::AudioBuffer>(this);
    connect(enhanceWatcher_, &QFutureWatcher<tts::AudioBuffer>::finished, this, &MainWindow::onEnhanceFinished);

    stopKaggleWatcher_ = new QFutureWatcher<bool>(this);
    connect(stopKaggleWatcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::onStopKaggleFinished);

    exportWatcher_ = new QFutureWatcher<bool>(this);
    connect(exportWatcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::onExportFinished);

    previewWatcher_ = new QFutureWatcher<SynthOutput>(this);
    connect(previewWatcher_, &QFutureWatcher<SynthOutput>::finished, this, &MainWindow::onPreviewFinished);

    batchExportWatcher_ = new QFutureWatcher<bool>(this);
    connect(batchExportWatcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::onBatchItemExportFinished);

    batchQueueDialog_ = new BatchQueueDialog(this);
    connect(batchQueueDialog_, &BatchQueueDialog::processItemRequested, this, &MainWindow::onBatchItemRequested);
    connect(batchQueueDialog_, &BatchQueueDialog::cancelRequested, this, &MainWindow::onBatchCancelRequested);

    segmentRerenderWatcher_ = new QFutureWatcher<tts::AudioBuffer>(this);
    connect(segmentRerenderWatcher_, &QFutureWatcher<tts::AudioBuffer>::finished, this,
            &MainWindow::onTimelineRerenderFinished);

    edgeTestWatcher_ = new QFutureWatcher<tts::EdgeTtsEngine::ConnectionTestResult>(this);
    connect(edgeTestWatcher_, &QFutureWatcher<tts::EdgeTtsEngine::ConnectionTestResult>::finished, this,
            &MainWindow::onTestEdgeTtsFinished);
    connect(remoteGpuPanel_, &RemoteGpuPanel::testEdgeTtsRequested, this, &MainWindow::onTestEdgeTtsRequested);
    connect(timelinePanel_, &TimelinePanel::rerenderSegmentRequested, this, &MainWindow::onTimelineRerenderRequested);

    playbackTimer_ = new QTimer(this);
    playbackTimer_->setInterval(50);
    connect(playbackTimer_, &QTimer::timeout, this, &MainWindow::onPlaybackTick);

    auto* synthShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(synthShortcut, &QShortcut::activated, this, &MainWindow::onSynthesizeClicked);
    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &MainWindow::onSaveProject);
    auto* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playShortcut, &QShortcut::activated, this, [this]() {
        if (audioSink_ && audioSink_->state() != QtAudio::StoppedState) {
            onPauseClicked();
        } else {
            onPlayClicked();
        }
    });
    auto* previewShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this);
    connect(previewShortcut, &QShortcut::activated, this, &MainWindow::onPreviewSelectionClicked);

    tagInserter_->setProviderIndex(providerCombo_->currentIndex());
    providerHints_->setProviderIndex(providerCombo_->currentIndex());
    multiPanel_->setChunkSettings(maxChunkChars_, sentenceGapMs_, paragraphGapMs_);

    auto refreshSingleTextStats = [this]() {
        textStats_->updateStats(textEdit_->toPlainText(), speedSlider_->value() / 100.0f, maxChunkChars_,
                                sentenceGapMs_, paragraphGapMs_);
    };
    refreshSingleTextStats();
    connect(textEdit_->document(), &QTextDocument::contentsChanged, this, [this, refreshSingleTextStats]() {
        refreshSingleTextStats();
        markProjectDirty();
    });
    connect(speedSlider_, &QSlider::valueChanged, this, refreshSingleTextStats);
    connect(remoteGpuPanel_, &RemoteGpuPanel::statusChanged, this, &MainWindow::updateStatusBar);

    tts::refreshPiperVoices(piperModelDir_.toStdString());

    // synthesisProgress/multiSynthesisProgress are emitted from background
    // threads; Qt auto-queues these back to the UI thread.
    // Switch the bar from indeterminate (range 0,0) to a real percentage
    // the first time a chunk completion fires (multi-chunk synthesis).
    connect(this, &MainWindow::synthesisProgress, this, [this](int percent) {
        if (progressBar_->maximum() == 0) {
            progressBar_->setRange(0, 100);
        }
        progressBar_->setValue(percent);
    });
    connect(this, &MainWindow::synthesisStatusMessage, this, &MainWindow::setStatus);
    connect(this, &MainWindow::multiSynthesisProgress, multiPanel_, &MultiSpeakerPanel::setProgress);

    QTimer::singleShot(0, this, &MainWindow::showFirstRunIfNeeded);
    QTimer::singleShot(100, this, &MainWindow::offerAutosaveRecovery);
}

MainWindow::~MainWindow() {
    if (audioSink_) {
        audioSink_->stop();
    }
}

void MainWindow::setStatus(const QString& text) {
    statusBar()->showMessage(text, 5000);
}

void MainWindow::updateStatusBar() {
    if (rawAudio_.sampleRate > 0 && !processedAudio_.empty()) {
        const double seconds =
            static_cast<double>(processedAudio_.size()) / static_cast<double>(rawAudio_.sampleRate);
        statusBarDuration_->setText(QString("Audio: %1s").arg(seconds, 0, 'f', 1));
    } else {
        statusBarDuration_->setText("Audio: —");
    }
    statusBarDirty_->setText(projectDirty_ ? "● Unsaved" : "Saved");
    statusBarDirty_->setStyleSheet(projectDirty_ ? "color: #e6a817; font-weight: bold;"
                                                 : "color: palette(mid);");
    if (remoteGpuPanel_) {
        statusBarRemote_->setText(remoteGpuPanel_->statusSummary());
    }
}

void MainWindow::updateGpuStatusLabel() {
    QString ep = "CPU";
    if (supertonicEngine_) {
        ep = supertonicEngine_->usingGpu()
                 ? QString("GPU: %1").arg(QString::fromStdString(supertonicEngine_->gpuName()))
                 : QString("CPU");
    }
    statusBarGpu_->setText(ep);
}

void MainWindow::setupVoiceCompleter(QComboBox* combo) {
    // Type-to-search + a neat fixed-height scrollable dropdown.
    tts::applyNeatComboPopup(combo, /*searchable=*/true);
}

bool MainWindow::validateCurrentProvider(const tts::VoiceEntry* voiceA, QString* errorOut) const {
    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const tts::ProviderValidationResult result = tts::validateProviderReady(
        provider, QCoreApplication::applicationDirPath().toStdString(), voiceA, remoteGpuPanel_->kokoroEnabled(),
        remoteGpuPanel_->kokoroUrl().toStdString(), remoteGpuPanel_->fishEnabled(),
        remoteGpuPanel_->fishUrl().toStdString());
    if (!result.ok && errorOut) {
        *errorOut = QString::fromStdString(result.message);
    }
    return result.ok;
}

void MainWindow::markProjectDirty() {
    if (!projectDirty_) {
        projectDirty_ = true;
        updateWindowTitle();
        updateStatusBar();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (projectDirty_) {
        QMessageBox msg(this);
        msg.setWindowTitle("Unsaved Changes");
        msg.setText("Save changes before closing?");
        msg.setIcon(QMessageBox::Warning);
        auto* saveBtn = msg.addButton("Save", QMessageBox::AcceptRole);
        auto* discardBtn = msg.addButton("Discard", QMessageBox::DestructiveRole);
        auto* cancelBtn = msg.addButton(QMessageBox::Cancel);
        msg.exec();

        if (msg.clickedButton() == cancelBtn) {
            event->ignore();
            return;
        }
        if (msg.clickedButton() == saveBtn) {
            if (currentProjectPath_.isEmpty()) {
                onSaveProjectAs();
            } else {
                onSaveProject();
            }
            if (projectDirty_) {
                event->ignore();
                return;
            }
        }
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::onAutosaveTick() {
    if (!projectDirty_) {
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/autosave.edgettsproj";
    if (ProjectManager::saveProject(path, captureProjectData())) {
        setStatus("Autosaved to " + path);
    }
}

void MainWindow::onImportTextClicked() {
    const QString path = QFileDialog::getOpenFileName(this, "Import Text", QString(),
                                                      "Text Files (*.txt);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import Text", "Could not read the selected file.");
        return;
    }
    textEdit_->setPlainText(QString::fromUtf8(file.readAll()));
    markProjectDirty();
}

void MainWindow::onMultiImportText() {
    markProjectDirty();
}

void MainWindow::runPreviewSynth(const SynthRequest& req, QPushButton* disableBtn) {
    previewDisableBtn_ = disableBtn;
    if (disableBtn) {
        disableBtn->setEnabled(false);
    }
    captureRemoteGpuState();
    humanizerEnabled_ = humanizerCheckBox_->isChecked();
    setStatus("Previewing...");

    SynthRequest previewReq = req;
    previewReq.dictionary =
        pronunciationPanel_ ? pronunciationPanel_->dictionary() : std::vector<tts::PronunciationEntry>{};

    auto future = QtConcurrent::run([this, previewReq]() -> SynthOutput {
        return runSynthesis(previewReq);
    });
    previewWatcher_->setFuture(future);
}

void MainWindow::onVoicePreviewClicked() {
    if (static_cast<tts::Provider>(providerCombo_->currentIndex()) == tts::Provider::Supertonic &&
        !supertonicEngine_) {
        QMessageBox::warning(this, "Not ready", "Supertonic models are still loading.");
        return;
    }

    SynthRequest req;
    req.text = "Hello, this is a voice preview.";
    req.speed = speedSlider_->value() / 100.0f;
    req.provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const auto& voices = tts::voicesForProvider(req.provider);
    const int idxA = comboVoiceFullIndex(voiceCombo_);
    if (idxA < 0 || idxA >= static_cast<int>(voices.size())) {
        return;
    }
    req.voiceA = voices[static_cast<size_t>(idxA)];
    req.mixEnabled = tts::supportsVoiceMixing(req.provider) && mixCheckBox_->isChecked();
    if (req.mixEnabled) {
        const int idxB = comboVoiceFullIndex(voiceBCombo_);
        if (idxB >= 0 && idxB < static_cast<int>(voices.size())) {
            req.voiceB = voices[static_cast<size_t>(idxB)];
            req.pctA = mixSlider_->value();
        } else {
            req.mixEnabled = false;
        }
    }
    runPreviewSynth(req, voicePreviewBtn_);
}

void MainWindow::onMultiPreviewSelectionRequested(const QString& text) {
    const DialogueSegment seg = multiPanel_->currentSpeakerLine(text);
    if (seg.text.empty()) {
        QMessageBox::warning(this, "Preview", "Select some script text to preview.");
        return;
    }
    if (seg.provider == tts::Provider::Supertonic && !supertonicEngine_) {
        QMessageBox::warning(this, "Not ready", "Supertonic models are still loading.");
        return;
    }

    SynthRequest req;
    req.text = seg.text;
    req.speed = seg.speed;
    req.provider = seg.provider;
    req.voiceA = seg.voiceA;
    req.mixEnabled = seg.mixEnabled;
    req.voiceB = seg.voiceB;
    req.pctA = seg.pctA;
    runPreviewSynth(req, multiPanel_->previewButton());
}

void MainWindow::seekPlayback(double seconds) {
    if (processedAudio_.empty() || rawAudio_.sampleRate <= 0) {
        return;
    }

    const double totalSec =
        static_cast<double>(processedAudio_.size()) / static_cast<double>(rawAudio_.sampleRate);
    seconds = std::clamp(seconds, 0.0, totalSec);

    if (audioSink_) {
        audioSink_->stop();
        delete audioSink_;
        audioSink_ = nullptr;
    }

    playbackStartOffsetSec_ = seconds;
    const size_t startSample =
        static_cast<size_t>(seconds * static_cast<double>(rawAudio_.sampleRate));
    if (startSample >= processedAudio_.size()) {
        return;
    }

    std::vector<float> slice(processedAudio_.begin() + static_cast<std::ptrdiff_t>(startSample),
                             processedAudio_.end());
    if (slice.empty()) {
        return;
    }

    QAudioFormat format;
    format.setSampleRate(rawAudio_.sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    audioSink_ = new QAudioSink(format, this);
    std::vector<int16_t> pcm = toInt16(slice);
    playbackBytes_ = QByteArray(reinterpret_cast<const char*>(pcm.data()),
                                 static_cast<int>(pcm.size() * sizeof(int16_t)));
    playbackBuffer_.close();
    playbackBuffer_.setBuffer(&playbackBytes_);
    playbackBuffer_.open(QIODevice::ReadOnly);
    audioSink_->start(&playbackBuffer_);

    stopBtn_->setEnabled(true);
    pauseBtn_->setEnabled(true);
    pauseBtn_->setText("Pause");
    multiPanel_->setPauseLabel("Pause");
    if (playbackTimer_) {
        playbackTimer_->start();
    }
    if (timelinePanel_) {
        timelinePanel_->setPlaybackPositionSeconds(seconds);
    }
}

void MainWindow::updateVoiceFilterUi() {
    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    if (voiceFilterWidget_ != nullptr) {
        voiceFilterWidget_->setVisible(provider == tts::Provider::EdgeTts);
    }
}

void MainWindow::selectVoiceAByFullIndex(int fullIdx) {
    if (fullIdx < 0) return;
    int pos = voiceCombo_->findData(fullIdx);
    if (pos < 0) {
        // Hidden by an active filter — clear filters and repopulate.
        if (voiceGenderFilter_) voiceGenderFilter_->setCurrentIndex(0);
        if (voiceLangFilter_) voiceLangFilter_->setCurrentIndex(0);
        populateVoiceCombos();
        pos = voiceCombo_->findData(fullIdx);
    }
    if (pos >= 0) voiceCombo_->setCurrentIndex(pos);
}

void MainWindow::selectVoiceBByFullIndex(int fullIdx) {
    if (fullIdx < 0) return;
    const int pos = voiceBCombo_->findData(fullIdx);
    if (pos >= 0) voiceBCombo_->setCurrentIndex(pos);
}

void MainWindow::populateVoiceCombos() {
    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    if (provider == tts::Provider::Piper) {
        tts::refreshPiperVoices(piperModelDir_.toStdString());
    }
    const auto& voices = tts::voicesForProvider(provider);

    // Read active filters (only meaningful for Edge; ignored otherwise).
    const bool isEdge = (provider == tts::Provider::EdgeTts);
    const QString genderSel = (isEdge && voiceGenderFilter_) ? voiceGenderFilter_->currentText() : QString();
    const QString langSel = (isEdge && voiceLangFilter_ && voiceLangFilter_->currentIndex() > 0)
                                ? voiceLangFilter_->currentText() : QString();
    const bool filterMale = (genderSel == "Male");
    const bool filterFemale = (genderSel == "Female");

    voiceCombo_->blockSignals(true);
    voiceBCombo_->blockSignals(true);
    voiceCombo_->clear();
    voiceBCombo_->clear();
    for (int i = 0; i < static_cast<int>(voices.size()); ++i) {
        const auto& v = voices[i];
        if (isEdge) {
            if (filterMale && tts::voiceGender(v) != "Male") continue;
            if (filterFemale && tts::voiceGender(v) != "Female") continue;
            if (!langSel.isEmpty() &&
                QString::fromStdString(tts::edgeVoiceLocale(v)) != langSel) continue;
        }
        const QString label = QString::fromStdString(v.friendlyName);
        // Store the full-provider-list index so selection survives filtering.
        voiceCombo_->addItem(label, i);
        voiceBCombo_->addItem(label, i);
    }
    voiceCombo_->blockSignals(false);
    voiceBCombo_->blockSignals(false);
    setupVoiceCompleter(voiceCombo_);
    setupVoiceCompleter(voiceBCombo_);

    // For Fish Speech, decorate slot labels with their saved status so users
    // know which slots contain a saved reference voice.
    if (provider == tts::Provider::FishSpeech) {
        updateFishVoiceComboLabels();
    }

    if (voiceBCombo_->count() > 1) {
        voiceBCombo_->setCurrentIndex(1);
    }
}

void MainWindow::updateMixUiEnabled() {
    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const bool supportsMix = tts::supportsVoiceMixing(provider);
    mixCheckBox_->setVisible(supportsMix);
    mixCheckBox_->setEnabled(supportsMix);
    if (!supportsMix) {
        mixCheckBox_->setChecked(false);
    }
    const bool mixOn = supportsMix && mixCheckBox_->isChecked();
    if (mixDetailWidget_) {
        mixDetailWidget_->setVisible(mixOn);
    }
    voiceBCombo_->setEnabled(mixOn);
    mixSlider_->setEnabled(mixOn);
    mixLabel_->setEnabled(mixOn);
}

void MainWindow::onProviderChanged(int /*index*/) {
    populateVoiceCombos();
    updateVoiceFilterUi();
    updateMixUiEnabled();
    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    humanizerCheckBox_->setEnabled(provider == tts::Provider::EdgeTts);
    if (humanizerSettingsBtn_ != nullptr) {
        humanizerSettingsBtn_->setEnabled(provider == tts::Provider::EdgeTts);
    }
    const bool isFish = (provider == tts::Provider::FishSpeech);
    fishPanel_->setVisible(isFish);
    if (tagInserter_) {
        tagInserter_->setProviderIndex(static_cast<int>(provider));
    }
    if (providerHints_) {
        providerHints_->setProviderIndex(static_cast<int>(provider));
    }
}

void MainWindow::onEngineLoaded() {
    try {
        supertonicEngine_ = engineWatcher_->result();
        setStatus("Ready");
        synthesizeBtn_->setEnabled(true);
        updateGpuStatusLabel();
        updateStatusBar();
    } catch (const std::exception& e) {
        setStatus(QString("Failed to load models: ") + e.what());
        QMessageBox::critical(this, "Model load error", e.what());
    }
}

void MainWindow::onGpuToggled(bool checked) {
    tts::setGpuPreferred(checked);
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue("useGpu", checked);

    // Lazily-constructed engines: drop them so they rebuild with the new
    // execution provider on next use (synthesizeSegment/synthesizeSegmentMixed
    // already null-check these).
    kokoroEngine_.reset();
    kokoroDeEngine_.reset();
    kokoroDeVictoriaEngine_.reset();
    piperEngines_.clear();
    dfnEngine_.reset();
    edgeTtsEngine_.reset();
    remoteKokoroEngine_.reset();
    remoteFishEngine_.reset();

    // Supertonic is loaded eagerly at startup on a background thread; redo
    // that load now so it picks up the new execution provider.
    synthesizeBtn_->setEnabled(false);
    setStatus(checked ? "Switching to GPU, reloading models..." : "Switching to CPU, reloading models...");
    const std::string modelDirStd = modelDir_.toStdString();
    auto future = QtConcurrent::run([modelDirStd]() -> std::shared_ptr<tts::SupertonicEngine> {
        return std::make_shared<tts::SupertonicEngine>(modelDirStd);
    });
    engineWatcher_->setFuture(future);
}

void MainWindow::onRemoteKokoroToggled(bool checked) {
    if (checked && remoteGpuPanel_->kokoroUrl().isEmpty()) {
        QMessageBox::information(this, "Remote Kokoro",
                                 "Enter the Kokoro Kaggle tunnel URL in the Remote GPU panel.");
    }
    if (static_cast<tts::Provider>(providerCombo_->currentIndex()) == tts::Provider::Kokoro) {
        setStatus(checked ? "Kokoro will use the remote Kaggle server." : "Kokoro will use local models.");
    }
    updateStatusBar();
}

void MainWindow::captureRemoteGpuState() {
    remoteKokoroEnabled_ = remoteGpuPanel_->kokoroEnabled();
    remoteKokoroUrl_ = remoteGpuPanel_->kokoroUrl();
    if (remoteKokoroEnabled_ && !remoteKokoroUrl_.isEmpty()) {
        remoteKokoroEngine_ = std::make_shared<tts::RemoteKokoroEngine>(remoteKokoroUrl_.toStdString());
    }
    remoteFishEnabled_ = remoteGpuPanel_->fishEnabled();
    remoteFishUrl_ = remoteGpuPanel_->fishUrl();
    if (remoteFishEnabled_ && !remoteFishUrl_.isEmpty()) {
        remoteFishEngine_ = std::make_shared<tts::RemoteFishSpeechEngine>(remoteFishUrl_.toStdString());
    }
}

void MainWindow::onStopKaggleClicked() {
    const QString url = remoteGpuPanel_->kokoroUrl();
    if (url.isEmpty()) {
        QMessageBox::information(this, "Stop Kaggle Session", "Enter the Kokoro Kaggle tunnel URL first.");
        return;
    }
    setStatus("Stopping Kokoro Kaggle session...");
    const std::string urlStd = url.toStdString();
    stopKaggleWatcher_->setFuture(QtConcurrent::run([urlStd]() { return tts::RemoteKokoroEngine::stopSession(urlStd); }));
}

void MainWindow::onStopKaggleFinished() {
    setStatus(stopKaggleWatcher_->result() ? "Kaggle session stopped." : "Ready");
}

void MainWindow::onEqDialogClicked() {
    if (eqDialog_->isVisible()) {
        eqDialog_->hide();
        eqDialogBtn_->setChecked(false);
    } else {
        // Position dialog below the main window toolbar area on first show.
        if (!eqDialog_->property("positioned").toBool()) {
            const QPoint p = mapToGlobal(QPoint(0, height()));
            eqDialog_->move(p.x(), p.y() - eqDialog_->sizeHint().height() - 30);
            eqDialog_->setProperty("positioned", true);
        }
        eqDialog_->show();
        eqDialog_->raise();
        eqDialogBtn_->setChecked(true);
    }
}

void MainWindow::onHumanizerSettingsClicked() {
    QDialog dialog(this);
    dialog.setWindowTitle("Natural Humanizer — Stages");

    auto* outer = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(
        "Toggle each stage and set its degree. Stages run top-to-bottom.\n"
        "Tip: De-Robot EQ has the biggest effect on naturalness.", &dialog);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    tts::HumanizerSettings& h = humanizerSettings_;

    // Helper to build a double spin box.
    auto makeSpin = [&dialog](double mn, double mx, double step, double val,
                              const QString& suffix) {
        auto* s = new QDoubleSpinBox(&dialog);
        s->setRange(mn, mx);
        s->setSingleStep(step);
        s->setValue(val);
        s->setDecimals(step < 0.1 ? 2 : 1);
        if (!suffix.isEmpty()) s->setSuffix(suffix);
        return s;
    };

    // --- 1. De-Robot EQ ---
    auto* eqGroup = new QGroupBox("1. De-Robot EQ — removes boxy/robotic resonance", &dialog);
    eqGroup->setCheckable(true);
    eqGroup->setChecked(h.eqEnabled);
    auto* eqForm = new QFormLayout(eqGroup);
    auto* eqWarmth = makeSpin(-12.0, 12.0, 0.5, h.eqWarmthDb, " dB");
    auto* eqMid    = makeSpin(-12.0, 12.0, 0.5, h.eqMidCutDb, " dB");
    auto* eqAir    = makeSpin(-12.0, 12.0, 0.5, h.eqAirDb, " dB");
    eqForm->addRow("Warmth (low shelf @195 Hz):", eqWarmth);
    eqForm->addRow("Mid cut (peak @2.1 kHz):", eqMid);
    eqForm->addRow("Air (high shelf @5.9 kHz):", eqAir);
    outer->addWidget(eqGroup);

    // --- 2. Compressor ---
    auto* compGroup = new QGroupBox("2. Compressor — even, consistent levels", &dialog);
    compGroup->setCheckable(true);
    compGroup->setChecked(h.compEnabled);
    auto* compForm = new QFormLayout(compGroup);
    auto* compThresh = makeSpin(-60.0, 0.0, 1.0, h.compThresholdDb, " dB");
    auto* compRatio  = makeSpin(1.0, 12.0, 0.5, h.compRatio, ":1");
    auto* compMakeup = makeSpin(0.0, 12.0, 0.5, h.compMakeupDb, " dB");
    compForm->addRow("Threshold:", compThresh);
    compForm->addRow("Ratio:", compRatio);
    compForm->addRow("Makeup gain:", compMakeup);
    outer->addWidget(compGroup);

    // --- 3. Reverb ---
    auto* revGroup = new QGroupBox("3. Subtle Reverb — natural \"space\"", &dialog);
    revGroup->setCheckable(true);
    revGroup->setChecked(h.reverbEnabled);
    auto* revForm = new QFormLayout(revGroup);
    auto* revWet = makeSpin(0.0, 50.0, 1.0, h.reverbWet * 100.0, " %");
    revForm->addRow("Wet amount:", revWet);
    outer->addWidget(revGroup);

    // --- 4. De-Esser ---
    auto* deGroup = new QGroupBox("4. De-Esser — tames harsh \"sss/t/z\"", &dialog);
    deGroup->setCheckable(true);
    deGroup->setChecked(h.deEsserEnabled);
    auto* deForm = new QFormLayout(deGroup);
    auto* deThresh = makeSpin(-60.0, 0.0, 1.0, h.deEsserThreshDb, " dB");
    auto* deRatio  = makeSpin(1.0, 12.0, 0.5, h.deEsserRatio, ":1");
    deForm->addRow("Threshold (lower = stronger):", deThresh);
    deForm->addRow("Ratio:", deRatio);
    outer->addWidget(deGroup);

    // --- 5. Loudness ---
    auto* loudGroup = new QGroupBox("5. Loudness — broadcast-consistent volume", &dialog);
    loudGroup->setCheckable(true);
    loudGroup->setChecked(h.loudnessEnabled);
    auto* loudForm = new QFormLayout(loudGroup);
    auto* loudTarget = makeSpin(-40.0, -6.0, 1.0, h.loudnessTargetDb, " dBFS");
    loudForm->addRow("Target RMS:", loudTarget);
    outer->addWidget(loudGroup);

    // --- 6. Safety ceiling ---
    auto* ceilGroup = new QGroupBox("6. Safety Ceiling — click-free, never clips", &dialog);
    ceilGroup->setCheckable(true);
    ceilGroup->setChecked(h.ceilingEnabled);
    auto* ceilForm = new QFormLayout(ceilGroup);
    auto* ceilDb = makeSpin(-6.0, 0.0, 0.5, h.ceilingDb, " dBFS");
    ceilForm->addRow("Peak ceiling:", ceilDb);
    outer->addWidget(ceilGroup);

    // Buttons: Restore Defaults + OK/Cancel
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&]() {
        const tts::HumanizerSettings d;
        eqGroup->setChecked(d.eqEnabled);
        eqWarmth->setValue(d.eqWarmthDb); eqMid->setValue(d.eqMidCutDb); eqAir->setValue(d.eqAirDb);
        compGroup->setChecked(d.compEnabled);
        compThresh->setValue(d.compThresholdDb); compRatio->setValue(d.compRatio);
        compMakeup->setValue(d.compMakeupDb);
        revGroup->setChecked(d.reverbEnabled); revWet->setValue(d.reverbWet * 100.0);
        deGroup->setChecked(d.deEsserEnabled);
        deThresh->setValue(d.deEsserThreshDb); deRatio->setValue(d.deEsserRatio);
        loudGroup->setChecked(d.loudnessEnabled); loudTarget->setValue(d.loudnessTargetDb);
        ceilGroup->setChecked(d.ceilingEnabled); ceilDb->setValue(d.ceilingDb);
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Pull values back into the settings struct.
    h.eqEnabled    = eqGroup->isChecked();
    h.eqWarmthDb   = static_cast<float>(eqWarmth->value());
    h.eqMidCutDb   = static_cast<float>(eqMid->value());
    h.eqAirDb      = static_cast<float>(eqAir->value());
    h.compEnabled     = compGroup->isChecked();
    h.compThresholdDb = static_cast<float>(compThresh->value());
    h.compRatio       = static_cast<float>(compRatio->value());
    h.compMakeupDb    = static_cast<float>(compMakeup->value());
    h.reverbEnabled = revGroup->isChecked();
    h.reverbWet     = static_cast<float>(revWet->value() / 100.0);
    h.deEsserEnabled  = deGroup->isChecked();
    h.deEsserThreshDb = static_cast<float>(deThresh->value());
    h.deEsserRatio    = static_cast<float>(deRatio->value());
    h.loudnessEnabled  = loudGroup->isChecked();
    h.loudnessTargetDb = static_cast<float>(loudTarget->value());
    h.ceilingEnabled = ceilGroup->isChecked();
    h.ceilingDb      = static_cast<float>(ceilDb->value());

    // Persist.
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue("hum/eqEnabled", h.eqEnabled);
    settings.setValue("hum/eqWarmthDb", h.eqWarmthDb);
    settings.setValue("hum/eqMidCutDb", h.eqMidCutDb);
    settings.setValue("hum/eqAirDb", h.eqAirDb);
    settings.setValue("hum/compEnabled", h.compEnabled);
    settings.setValue("hum/compThresholdDb", h.compThresholdDb);
    settings.setValue("hum/compRatio", h.compRatio);
    settings.setValue("hum/compMakeupDb", h.compMakeupDb);
    settings.setValue("hum/reverbEnabled", h.reverbEnabled);
    settings.setValue("hum/reverbWet", h.reverbWet);
    settings.setValue("hum/deEsserEnabled", h.deEsserEnabled);
    settings.setValue("hum/deEsserThreshDb", h.deEsserThreshDb);
    settings.setValue("hum/deEsserRatio", h.deEsserRatio);
    settings.setValue("hum/loudnessEnabled", h.loudnessEnabled);
    settings.setValue("hum/loudnessTargetDb", h.loudnessTargetDb);
    settings.setValue("hum/ceilingEnabled", h.ceilingEnabled);
    settings.setValue("hum/ceilingDb", h.ceilingDb);

    // If the master toggle is off, hint that these only apply when it's on.
    if (!humanizerCheckBox_->isChecked()) {
        statusBar()->showMessage(
            "Humanizer stages saved — enable 'Natural Humanizer (Edge TTS)' to hear them.", 5000);
    }
}

void MainWindow::onSettingsClicked() {
    QDialog dialog(this);
    dialog.setWindowTitle("Chunking & Pause Settings");

    auto* form = new QFormLayout(&dialog);

    auto* maxCharsSpin = new QSpinBox(&dialog);
    maxCharsSpin->setRange(100, 2000);
    maxCharsSpin->setValue(maxChunkChars_);
    form->addRow("Max characters per chunk:", maxCharsSpin);

    auto* sentenceGapSpin = new QSpinBox(&dialog);
    sentenceGapSpin->setRange(0, 2000);
    sentenceGapSpin->setSuffix(" ms");
    sentenceGapSpin->setValue(sentenceGapMs_);
    form->addRow("Sentence gap:", sentenceGapSpin);

    auto* paragraphGapSpin = new QSpinBox(&dialog);
    paragraphGapSpin->setRange(0, 5000);
    paragraphGapSpin->setSuffix(" ms");
    paragraphGapSpin->setValue(paragraphGapMs_);
    form->addRow("Paragraph gap:", paragraphGapSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    maxChunkChars_ = maxCharsSpin->value();
    sentenceGapMs_ = sentenceGapSpin->value();
    paragraphGapMs_ = paragraphGapSpin->value();

    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue("maxChunkChars", maxChunkChars_);
    settings.setValue("sentenceGapMs", sentenceGapMs_);
    settings.setValue("paragraphGapMs", paragraphGapMs_);

    multiPanel_->setChunkSettings(maxChunkChars_, sentenceGapMs_, paragraphGapMs_);
    textStats_->updateStats(textEdit_->toPlainText(), speedSlider_->value() / 100.0f, maxChunkChars_,
                            sentenceGapMs_, paragraphGapMs_);
}

void MainWindow::onSynthesizeClicked() {
    SynthRequest req;
    req.text = textEdit_->toPlainText().toStdString();
    if (req.text.empty()) {
        QMessageBox::warning(this, "No text", "Please enter some text to synthesize.");
        return;
    }
    req.speed = speedSlider_->value() / 100.0f;
    req.provider = static_cast<tts::Provider>(providerCombo_->currentIndex());

    if (req.provider == tts::Provider::Supertonic && !supertonicEngine_) {
        QMessageBox::warning(this, "Not ready", "Supertonic models are still loading. Please wait.");
        return;
    }

    const auto& voices = tts::voicesForProvider(req.provider);
    const int idxA = comboVoiceFullIndex(voiceCombo_);
    if (idxA < 0 || idxA >= static_cast<int>(voices.size())) {
        return;
    }
    req.voiceA = voices[idxA];

    req.mixEnabled = tts::supportsVoiceMixing(req.provider) && mixCheckBox_->isChecked();
    if (req.mixEnabled) {
        const int idxB = comboVoiceFullIndex(voiceBCombo_);
        if (idxB < 0 || idxB >= static_cast<int>(voices.size())) {
            req.mixEnabled = false;
        } else {
            req.voiceB = voices[idxB];
            req.pctA = mixSlider_->value();
        }
    }

    captureRemoteGpuState();
    QString validationError;
    if (!validateCurrentProvider(&req.voiceA, &validationError)) {
        QMessageBox::warning(this, "Not ready", validationError);
        return;
    }

    humanizerEnabled_ = humanizerCheckBox_->isChecked();
    streamingPlayback_ = streamPlaybackCheckBox_->isChecked();
    partialPlaybackStarted_ = false;
    req.dictionary = pronunciationPanel_ ? pronunciationPanel_->dictionary() : std::vector<tts::PronunciationEntry>{};

    synthesisCancel_.store(false);
    synthesizeBtn_->setEnabled(false);
    cancelSynthBtn_->setEnabled(true);
    playBtn_->setEnabled(false);
    exportBtn_->setEnabled(false);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    setStatus("Synthesizing... 0% (starting)");

    auto future = QtConcurrent::run([this, req]() -> SynthOutput {
        try {
            return runSynthesis(req);
        } catch (const std::exception& e) {
            throw SynthesisException(e.what());
        }
    });
    synthWatcher_->setFuture(future);
}

SynthOutput MainWindow::runSynthesis(const SynthRequest& req) {
    constexpr int kTargetRate = 44100;
    const ChunkPlan plan = buildChunkPlan(req.text, req.provider, req.dictionary, maxChunkChars_, sentenceGapMs_,
                                          paragraphGapMs_);
    const auto& chunks = plan.chunks;
    const auto& chunkSpeakable = plan.chunkSpeakable;
    const auto& chunkPauseAfterMs = plan.chunkPauseAfterMs;

    auto synthOne = [&](const std::string& chunkText) -> std::vector<float> {
        tts::AudioBuffer buf = synthesizeSegmentMixed(req.provider, req.voiceA, req.mixEnabled, req.voiceB,
                                                       req.pctA, chunkText, req.speed);
        // Single-speaker Natural Humanizer (Edge voices only).
        if (humanizerEnabled_ && req.provider == tts::Provider::EdgeTts) {
            tts::applyHumanizer(buf, humanizerSettings_);
        }
        return tts::resampleLinear(buf.samples, buf.sampleRate, kTargetRate);
    };

    std::vector<TimelineSegment> timeline;
    timeline.reserve(chunks.size());

    const size_t totalChunks = chunks.size();
    emit synthesisProgress(0);
    emit synthesisStatusMessage(
        QString("Synthesizing... 0% (chunk 1 of %1)").arg(static_cast<int>(totalChunks)));

    std::vector<std::vector<float>> results(chunks.size());
    auto onChunkDone = [&](size_t j) {
        if (synthesisCancel_.load()) {
            throw SynthesisException("Synthesis cancelled.");
        }
        const int donePercent = static_cast<int>((j + 1) * 100 / totalChunks);
        emit synthesisProgress(donePercent);
        emit synthesisStatusMessage(QString("Synthesizing... %1% (chunk %2 of %3 done)")
                                        .arg(donePercent)
                                        .arg(static_cast<int>(j) + 1)
                                        .arg(static_cast<int>(totalChunks)));
        if (streamingPlayback_ && j == 0 && !results[0].empty()) {
            partialPlaybackSamples_ = results[0];
            partialPlaybackSampleRate_ = kTargetRate;
            QMetaObject::invokeMethod(this, &MainWindow::onPartialPlaybackReady, Qt::QueuedConnection);
        }
    };

    if (!chunks.empty()) {
        emit synthesisStatusMessage(QString("Synthesizing... 0% (chunk 1 of %1, working...)")
                                        .arg(static_cast<int>(totalChunks)));
        results[0] = synthOne(chunks[0]);
        onChunkDone(0);

        // Edge TTS uses one websocket per chunk; keep requests sequential so
        // cancel is responsive and the service is not flooded in parallel.
        const size_t concurrency = req.provider == tts::Provider::EdgeTts
                                       ? 1
                                       : std::max<size_t>(1, std::thread::hardware_concurrency() / 2);
        size_t i = 1;
        while (i < chunks.size()) {
            if (synthesisCancel_.load()) {
                throw SynthesisException("Synthesis cancelled.");
            }
            const size_t batchEnd = std::min(chunks.size(), i + concurrency);
            if (concurrency == 1) {
                for (size_t j = i; j < batchEnd; ++j) {
                    const int startPercent = static_cast<int>(j * 100 / totalChunks);
                    emit synthesisProgress(startPercent);
                    emit synthesisStatusMessage(QString("Synthesizing... %1% (chunk %2 of %3, working...)")
                                                    .arg(startPercent)
                                                    .arg(static_cast<int>(j) + 1)
                                                    .arg(static_cast<int>(totalChunks)));
                    results[j] = synthOne(chunks[j]);
                    onChunkDone(j);
                }
            } else {
                std::vector<std::future<std::vector<float>>> futures;
                futures.reserve(batchEnd - i);
                for (size_t j = i; j < batchEnd; ++j) {
                    const int startPercent = static_cast<int>(j * 100 / totalChunks);
                    emit synthesisProgress(startPercent);
                    emit synthesisStatusMessage(QString("Synthesizing... %1% (chunk %2 of %3, working...)")
                                                    .arg(startPercent)
                                                    .arg(static_cast<int>(j) + 1)
                                                    .arg(static_cast<int>(totalChunks)));
                    futures.push_back(std::async(std::launch::async, synthOne, chunks[j]));
                }
                for (size_t j = i; j < batchEnd; ++j) {
                    results[j] = futures[j - i].get();
                    onChunkDone(j);
                }
            }
            i = batchEnd;
        }
    }

    for (size_t j = 0; j < chunks.size(); ++j) {
        TimelineSegment seg;
        seg.text = chunkSpeakable[j];
        seg.label = makeSegmentLabel(seg.text);
        seg.samples = std::move(results[j]);
        seg.sampleRate = kTargetRate;
        seg.pauseAfterMs = chunkPauseAfterMs[j];
        timeline.push_back(std::move(seg));
    }

    return {timeline::buildFromTimeline(timeline), timeline};
}

tts::AudioBuffer MainWindow::synthesizeSegmentMixed(tts::Provider provider, const tts::VoiceEntry& voiceA,
                                                     bool mixEnabled, const tts::VoiceEntry& voiceB, int pctA,
                                                     const std::string& text, float speed) {
    const std::string spokenText =
        provider == tts::Provider::EdgeTts ? text : tts::stripMarkup(text);
    switch (provider) {
        case tts::Provider::Supertonic: {
            if (!mixEnabled) {
                return synthesizeSegment(provider, voiceA, spokenText, speed);
            }

            auto loadStyle = [this](const std::string& shortName) {
                const QString path = voiceStylesDir_ + "/" + QString::fromStdString(shortName) + ".json";
                return tts::VoiceStyle::loadFromJson(path.toStdString());
            };

            tts::VoiceStyle a = loadStyle(voiceA.shortName);
            tts::VoiceStyle b = loadStyle(voiceB.shortName);
            const int total = std::max(1, pctA + (100 - pctA));
            const float wa = static_cast<float>(pctA) / total;
            const float wb = static_cast<float>(100 - pctA) / total;
            tts::VoiceStyle style = tts::VoiceStyle::blend(a, b, wa, wb);

            tts::SynthParams params;
            params.text = spokenText;
            params.speed = std::clamp(speed, 0.7f, 2.0f);
            params.totalSteps = kDefaultTotalSteps;
            params.lang = "na";
            return supertonicEngine_->synthesize(params, style);
        }

        case tts::Provider::Kokoro: {
            if (!phonemizer_) {
                phonemizer_ = std::make_shared<tts::Phonemizer>(espeakDataDir_.toStdString());
            }
            // The German "martin"/"victoria" voices live in separate models with
            // their own style tables, so they can't be blended with the main
            // Kokoro voices.
            if (!mixEnabled || voiceA.shortName == "martin" || voiceB.shortName == "martin" ||
                voiceA.shortName == "victoria" || voiceB.shortName == "victoria") {
                return synthesizeSegment(provider, voiceA, spokenText, speed);
            }

            if (remoteKokoroEnabled_ && !remoteKokoroUrl_.isEmpty()) {
                const float clampedSpeed = std::clamp(speed, 0.5f, 2.0f);
                return remoteKokoroEngine_->synthesizeMixed(spokenText, voiceA.shortName, voiceB.shortName, pctA,
                                                              voiceA.espeakLang, clampedSpeed);
            }

            if (!kokoroEngine_) {
                kokoroEngine_ = std::make_shared<tts::KokoroEngine>(kokoroModelDir_.toStdString());
            }
            std::vector<float> style = kokoroEngine_->loadVoiceStyleTable(voiceA.shortName);
            std::vector<float> styleB = kokoroEngine_->loadVoiceStyleTable(voiceB.shortName);
            const int total = std::max(1, pctA + (100 - pctA));
            const float wa = static_cast<float>(pctA) / total;
            const float wb = static_cast<float>(100 - pctA) / total;
            for (size_t i = 0; i < style.size(); ++i) {
                style[i] = style[i] * wa + styleB[i] * wb;
            }

            const float clampedSpeed = std::clamp(speed, 0.5f, 2.0f);
            return kokoroEngine_->synthesize(*phonemizer_, spokenText, voiceA.espeakLang, style, clampedSpeed);
        }

        case tts::Provider::Piper:
        default:
            return synthesizeSegment(provider, voiceA, spokenText, speed);
    }
}

tts::AudioBuffer MainWindow::synthesizeSegment(tts::Provider provider, const tts::VoiceEntry& voice,
                                                const std::string& text, float speed) {
    const std::string spokenText =
        provider == tts::Provider::EdgeTts ? text : tts::stripMarkup(text);
    switch (provider) {
        case tts::Provider::Supertonic: {
            const QString path = voiceStylesDir_ + "/" + QString::fromStdString(voice.shortName) + ".json";
            tts::VoiceStyle style = tts::VoiceStyle::loadFromJson(path.toStdString());

            tts::SynthParams params;
            params.text = spokenText;
            params.speed = std::clamp(speed, 0.7f, 2.0f);
            params.totalSteps = kDefaultTotalSteps;
            params.lang = "na";
            return supertonicEngine_->synthesize(params, style);
        }

        case tts::Provider::Kokoro: {
            if (!phonemizer_) {
                phonemizer_ = std::make_shared<tts::Phonemizer>(espeakDataDir_.toStdString());
            }
            const float clampedSpeed = std::clamp(speed, 0.5f, 2.0f);
            if (voice.shortName == "martin") {
                if (!kokoroDeEngine_) {
                    kokoroDeEngine_ = std::make_shared<tts::KokoroEngine>(kokoroDeModelDir_.toStdString(),
                                                                           "kokoro-martin.onnx");
                }
                std::vector<float> style = kokoroDeEngine_->loadVoiceStyleTable(voice.shortName);
                return kokoroDeEngine_->synthesize(*phonemizer_, spokenText, voice.espeakLang, style, clampedSpeed);
            }
            if (voice.shortName == "victoria") {
                if (!kokoroDeVictoriaEngine_) {
                    kokoroDeVictoriaEngine_ = std::make_shared<tts::KokoroEngine>(
                        kokoroDeVictoriaModelDir_.toStdString(), "kokoro-victoria.onnx");
                }
                std::vector<float> style = kokoroDeVictoriaEngine_->loadVoiceStyleTable(voice.shortName);
                return kokoroDeVictoriaEngine_->synthesize(*phonemizer_, spokenText, voice.espeakLang, style,
                                                             clampedSpeed);
            }
            if (remoteKokoroEnabled_ && !remoteKokoroUrl_.isEmpty()) {
                return remoteKokoroEngine_->synthesize(spokenText, voice.shortName, voice.espeakLang, clampedSpeed);
            }
            if (!kokoroEngine_) {
                kokoroEngine_ = std::make_shared<tts::KokoroEngine>(kokoroModelDir_.toStdString());
            }
            std::vector<float> style = kokoroEngine_->loadVoiceStyleTable(voice.shortName);
            return kokoroEngine_->synthesize(*phonemizer_, spokenText, voice.espeakLang, style, clampedSpeed);
        }

        case tts::Provider::EdgeTts: {
            if (!edgeTtsEngine_) {
                edgeTtsEngine_ = std::make_shared<tts::EdgeTtsEngine>();
            }
            const float clampedSpeed = std::clamp(speed, 0.5f, 2.0f);
            // Natural Humanizer is applied by the caller (per-speaker in the
            // multi tab; via humanizerEnabled_ in the single-speaker tab) so the
            // right flag controls it for each path.
            return edgeTtsEngine_->synthesize(text, voice.shortName, clampedSpeed, false, 60000,
                                              &synthesisCancel_);
        }

        case tts::Provider::FishSpeech: {
            if (!remoteFishEnabled_ || remoteFishUrl_.isEmpty()) {
                throw std::runtime_error(
                    "Fish Audio S2 requires a Kaggle server. "
                    "Check 'Use Kaggle GPU (Fish Audio S2)' and enter the tunnel URL.");
            }
            if (!remoteFishEngine_) {
                remoteFishEngine_ = std::make_shared<tts::RemoteFishSpeechEngine>(
                    remoteFishUrl_.toStdString());
            }
            const float clampedSpeed = std::clamp(speed, 0.5f, 2.0f);

            // Resolve reference audio: slot_1/slot_2/slot_3 → load saved WAV from disk.
            // If the selected voice is "random" or no file saved for the slot, use empty
            // (random voice mode).
            if (voice.shortName != "random") {
                const int slot = static_cast<int>(voice.shortName.back() - '1'); // 0-based
                const QString voiceDir =
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/fish_voices";
                const QString tokenPath = voiceDir + QString("/slot_%1_tokens.json").arg(slot + 1);
                if (QFileInfo::exists(tokenPath)) {
                    QFile tokenFile(tokenPath);
                    if (tokenFile.open(QIODevice::ReadOnly)) {
                        return remoteFishEngine_->synthesizeWithReferenceJson(
                            spokenText, tokenFile.readAll().toStdString(), clampedSpeed);
                    }
                }
            }

            std::string refAudioB64;
            if (voice.shortName != "random") {
                const int slot = static_cast<int>(voice.shortName.back() - '1'); // 0-based
                QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
                const QString slotPath =
                    settings.value(QString("fishVoiceSlot_%1_path").arg(slot), "").toString();
                if (!slotPath.isEmpty()) {
                    QFile f(slotPath);
                    if (f.open(QIODevice::ReadOnly)) {
                        refAudioB64 = f.readAll().toBase64().toStdString();
                    }
                }
            }
            if (refAudioB64.empty() && !fishRefAudioPath_.isEmpty()) {
                QFile f(fishRefAudioPath_);
                if (f.open(QIODevice::ReadOnly)) {
                    refAudioB64 = f.readAll().toBase64().toStdString();
                }
            }

            return remoteFishEngine_->synthesize(spokenText, refAudioB64, /*refText=*/"", clampedSpeed);
        }

        case tts::Provider::Piper:
        default: {
            if (!phonemizer_) {
                phonemizer_ = std::make_shared<tts::Phonemizer>(espeakDataDir_.toStdString());
            }

            std::shared_ptr<tts::PiperEngine> engine;
            auto it = piperEngines_.find(voice.shortName);
            if (it != piperEngines_.end()) {
                engine = it->second;
            } else {
                const QString base = piperModelDir_ + "/" + QString::fromStdString(voice.shortName);
                engine = std::make_shared<tts::PiperEngine>((base + ".onnx").toStdString(),
                                                              (base + ".onnx.json").toStdString());
                piperEngines_[voice.shortName] = engine;
            }

            const float clampedSpeed = std::clamp(speed, 0.5f, 2.0f);
            return engine->synthesize(*phonemizer_, spokenText, clampedSpeed);
        }
    }
}

SynthOutput MainWindow::runMultiSynthesis(const MultiSynthRequest& req) {
    constexpr int kTargetRate = 44100;
    std::vector<TimelineSegment> timeline;

    size_t totalChunks = 0;
    std::vector<ChunkPlan> plans(req.segments.size());
    for (size_t i = 0; i < req.segments.size(); ++i) {
        plans[i] = buildChunkPlan(req.segments[i].text, req.segments[i].provider, pronunciationDictionary_,
                                  maxChunkChars_, sentenceGapMs_, paragraphGapMs_);
        totalChunks += plans[i].chunks.size();
    }

    size_t doneChunks = 0;
    for (size_t i = 0; i < req.segments.size(); ++i) {
        if (synthesisCancel_.load()) {
            throw SynthesisException("Synthesis cancelled.");
        }
        const auto& seg = req.segments[i];
        const ChunkPlan& plan = plans[i];

        auto synthOne = [&](const std::string& chunkText) -> std::vector<float> {
            tts::AudioBuffer buf = synthesizeSegmentMixed(seg.provider, seg.voiceA, seg.mixEnabled, seg.voiceB,
                                                            seg.pctA, chunkText, seg.speed);
            // Per-speaker Natural Humanizer (Edge voices only).
            if (seg.humanizerEnabled && seg.provider == tts::Provider::EdgeTts) {
                tts::applyHumanizer(buf, humanizerSettings_);
            }
            tts::GraphicEq eq;
            eq.setGainsDb(seg.eqGainsDb, static_cast<float>(buf.sampleRate));
            eq.process(buf.samples);
            return tts::resampleLinear(buf.samples, buf.sampleRate, kTargetRate);
        };

        for (size_t j = 0; j < plan.chunks.size(); ++j) {
            if (synthesisCancel_.load()) {
                throw SynthesisException("Synthesis cancelled.");
            }
            TimelineSegment tseg;
            tseg.text = plan.chunkSpeakable[j];
            tseg.label = makeSegmentLabel(tseg.text);
            tseg.samples = synthOne(plan.chunks[j]);
            tseg.sampleRate = kTargetRate;
            tseg.pauseAfterMs = plan.chunkPauseAfterMs[j];
            if (j + 1 == plan.chunks.size() && i + 1 < req.segments.size()) {
                tseg.pauseAfterMs += req.pauseMs;
            }
            timeline.push_back(std::move(tseg));
            ++doneChunks;
            emit multiSynthesisProgress(static_cast<int>(doneChunks * 100 / std::max<size_t>(1, totalChunks)));
        }
    }

    return {timeline::buildFromTimeline(timeline), timeline};
}

void MainWindow::onMultiRenderRequested(const MultiSynthRequest& req) {
    if (req.segments.empty()) {
        QMessageBox::warning(this, "No dialogue", "Please write at least one line like \"A: Hello there\".");
        return;
    }
    for (const auto& seg : req.segments) {
        if (seg.provider == tts::Provider::Supertonic && !supertonicEngine_) {
            QMessageBox::warning(this, "Not ready", "Supertonic models are still loading. Please wait.");
            return;
        }
        const auto vr = tts::validateProviderReady(
            seg.provider, QCoreApplication::applicationDirPath().toStdString(), &seg.voiceA,
            remoteGpuPanel_->kokoroEnabled(), remoteGpuPanel_->kokoroUrl().toStdString(),
            remoteGpuPanel_->fishEnabled(), remoteGpuPanel_->fishUrl().toStdString());
        if (!vr.ok) {
            QMessageBox::warning(this, "Not ready", QString::fromStdString(vr.message));
            return;
        }
    }

    captureRemoteGpuState();
    humanizerEnabled_ = humanizerCheckBox_->isChecked();
    pronunciationDictionary_ =
        pronunciationPanel_ ? pronunciationPanel_->dictionary() : std::vector<tts::PronunciationEntry>{};

    synthesisCancel_.store(false);
    multiPanel_->setBusy(true);
    multiPanel_->setPlaybackEnabled(false);
    multiPanel_->setProgress(0);
    multiPanel_->setStatus("Rendering dialogue...");

    auto future = QtConcurrent::run([this, req]() -> SynthOutput {
        try {
            return runMultiSynthesis(req);
        } catch (const std::exception& e) {
            throw SynthesisException(e.what());
        }
    });
    multiSynthWatcher_->setFuture(future);
}

void MainWindow::onMultiSynthesisFinished() {
    multiPanel_->setBusy(false);
    try {
        const SynthOutput output = multiSynthWatcher_->result();
        rawAudio_ = output.audio;
        setTimelineSegments(output.timeline);
    } catch (const std::exception& e) {
        multiPanel_->setStatus("Render failed");
        QMessageBox::critical(this, "Synthesis error", e.what());
        return;
    }

    reprocessAndRefreshPlayback();
    multiPanel_->setPlaybackEnabled(true);
    playBtn_->setEnabled(true);
    exportBtn_->setEnabled(true);
    markProjectDirty();
    updateStatusBar();

    const double seconds = rawAudio_.sampleRate > 0
                               ? static_cast<double>(rawAudio_.samples.size()) / rawAudio_.sampleRate
                               : 0.0;
    multiPanel_->setStatus(QString("Done (%1s audio)").arg(seconds, 0, 'f', 1));
}

void MainWindow::onSynthesisFinished() {
    synthesizeBtn_->setEnabled(true);
    cancelSynthBtn_->setEnabled(false);
    progressBar_->setRange(0, 100);
    try {
        const SynthOutput output = synthWatcher_->result();
        rawAudio_ = output.audio;
        setTimelineSegments(output.timeline);
    } catch (const std::exception& e) {
        progressBar_->setValue(0);
        setStatus(QString("Synthesis failed: %1").arg(e.what()));
        QMessageBox::critical(this, "Synthesis error", e.what());
        return;
    }

    reprocessAndRefreshPlayback();
    playBtn_->setEnabled(true);
    exportBtn_->setEnabled(true);
    progressBar_->setValue(100);
    markProjectDirty();
    updateStatusBar();

    const double seconds = rawAudio_.sampleRate > 0
                               ? static_cast<double>(rawAudio_.samples.size()) / rawAudio_.sampleRate
                               : 0.0;
    setStatus(QString("Done (%1s audio)").arg(seconds, 0, 'f', 1));
}

void MainWindow::onEqChanged() {
    if (!rawAudio_.samples.empty()) {
        reprocessAndRefreshPlayback();
    }
}

void MainWindow::reprocessAndRefreshPlayback() {
    processedAudio_ = rawAudio_.samples;

    tts::GraphicEq eq;
    eq.setGainsDb(eqPanel_->getGainsDb(), static_cast<float>(rawAudio_.sampleRate));
    eq.process(processedAudio_);

    tts::peakNormalize(processedAudio_);

    if (audioSink_ && audioSink_->state() != QtAudio::StoppedState) {
        audioSink_->stop();
    }
}

std::vector<int16_t> MainWindow::toInt16(const std::vector<float>& samples) const {
    std::vector<int16_t> out(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        float clamped = std::clamp(samples[i], -1.0f, 1.0f);
        out[i] = static_cast<int16_t>(std::lround(clamped * 32767.0f));
    }
    return out;
}

void MainWindow::onPlayClicked() {
    if (processedAudio_.empty()) {
        return;
    }
    if (audioSink_) {
        audioSink_->stop();
        delete audioSink_;
        audioSink_ = nullptr;
    }

    QAudioFormat format;
    format.setSampleRate(rawAudio_.sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    audioSink_ = new QAudioSink(format, this);

    std::vector<int16_t> pcm = toInt16(processedAudio_);
    playbackBytes_ = QByteArray(reinterpret_cast<const char*>(pcm.data()),
                                 static_cast<int>(pcm.size() * sizeof(int16_t)));
    playbackBuffer_.close();
    playbackBuffer_.setBuffer(&playbackBytes_);
    playbackBuffer_.open(QIODevice::ReadOnly);

    audioSink_->start(&playbackBuffer_);
    stopBtn_->setEnabled(true);
    pauseBtn_->setEnabled(true);
    pauseBtn_->setText("Pause");
    multiPanel_->setPauseLabel("Pause");
    if (playbackTimer_) {
        playbackTimer_->start();
    }
    playbackStartOffsetSec_ = 0.0;
    if (timelinePanel_) {
        timelinePanel_->setPlaybackPositionSeconds(0.0);
    }
}

void MainWindow::onPauseClicked() {
    if (!audioSink_) {
        return;
    }
    if (audioSink_->state() == QtAudio::ActiveState) {
        audioSink_->suspend();
        pauseBtn_->setText("Resume");
        multiPanel_->setPauseLabel("Resume");
    } else if (audioSink_->state() == QtAudio::SuspendedState) {
        audioSink_->resume();
        pauseBtn_->setText("Pause");
        multiPanel_->setPauseLabel("Pause");
    }
}

void MainWindow::onStopClicked() {
    if (audioSink_) {
        audioSink_->stop();
    }
    if (playbackTimer_) {
        playbackTimer_->stop();
    }
    playbackStartOffsetSec_ = 0.0;
    if (timelinePanel_) {
        timelinePanel_->setPlaybackPositionSeconds(-1.0);
    }
    pauseBtn_->setEnabled(false);
    pauseBtn_->setText("Pause");
    multiPanel_->setPauseLabel("Pause");
}

void MainWindow::onExportClicked() {
    if (processedAudio_.empty()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Export Audio", QString(),
                                                      tts::supportedExportFilters().join(";;"));
    if (path.isEmpty()) {
        return;
    }

    pendingExportPath_ = path;
    synthesizeBtn_->setEnabled(false);
    playBtn_->setEnabled(false);
    pauseBtn_->setEnabled(false);
    exportBtn_->setEnabled(false);
    multiPanel_->setBusy(true);
    multiPanel_->setPlaybackEnabled(false);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    multiPanel_->setProgress(0);
    setStatus("Exporting...");
    multiPanel_->setStatus("Exporting...");

    const std::vector<float> samples = processedAudio_;
    const uint32_t sampleRate = static_cast<uint32_t>(rawAudio_.sampleRate);
    const std::string pathStd = path.toStdString();

    const tts::AudioExportFormat format = tts::formatFromPath(pathStd);
    auto future = QtConcurrent::run([this, pathStd, samples, sampleRate, format]() -> bool {
        return tts::exportAudio(pathStd, format, samples, sampleRate, [this](int percent) {
            emit synthesisProgress(percent);
            emit multiSynthesisProgress(percent);
        });
    });
    exportWatcher_->setFuture(future);
}

void MainWindow::onExportFinished() {
    const bool ok = exportWatcher_->result();

    synthesizeBtn_->setEnabled(true);
    playBtn_->setEnabled(true);
    pauseBtn_->setEnabled(audioSink_ != nullptr && audioSink_->state() != QtAudio::StoppedState);
    exportBtn_->setEnabled(true);
    multiPanel_->setBusy(false);
    multiPanel_->setPlaybackEnabled(true);

    if (!ok) {
        setStatus("Export failed");
        multiPanel_->setStatus("Export failed");
        QMessageBox::critical(this, "Export failed",
                              "Could not write audio file. For MP3/FLAC, install ffmpeg or bundle "
                              "tools/ffmpeg.exe next to the app.\n\n" +
                                  pendingExportPath_);
        return;
    }

    setStatus("Exported to " + pendingExportPath_);
    multiPanel_->setStatus("Exported to " + pendingExportPath_);
}

// ---------------------------------------------------------------------------
// Fish Audio S2 Pro helpers + slots
// ---------------------------------------------------------------------------

void MainWindow::updateFishVoiceComboLabels() {
    // voiceCombo_ layout for Fish Speech:
    //   index 0 = "Fish Audio S2 - Random Voice"
    //   index 1 = "Fish Audio S2 - Voice Slot 1"
    //   index 2 = "Fish Audio S2 - Voice Slot 2"
    //   index 3 = "Fish Audio S2 - Voice Slot 3"
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    for (int s = 0; s < 3; ++s) {
        const QString path = settings.value(QString("fishVoiceSlot_%1_path").arg(s), "").toString();
        const bool hasSaved = !path.isEmpty() && QFile::exists(path);
        const QString label = hasSaved
            ? QString("Fish Audio S2 - Voice Slot %1  ✓ (saved)").arg(s + 1)
            : QString("Fish Audio S2 - Voice Slot %1  (empty — will use random)").arg(s + 1);
        // combo index 0 is "random", slots start at index 1
        if (voiceCombo_->count() > s + 1) voiceCombo_->setItemText(s + 1, label);
        // Also update the fishSlotCombo_ target labels
        const QString slotLabel = hasSaved
            ? QString("Slot %1  ✓ (has saved voice)").arg(s + 1)
            : QString("Slot %1  (empty)").arg(s + 1);
        if (fishSlotCombo_->count() > s) fishSlotCombo_->setItemText(s, slotLabel);
    }
}

void MainWindow::onFishBrowseClicked() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Select Reference Voice Audio", "",
        "Audio Files (*.wav *.mp3 *.flac *.ogg *.m4a);;All Files (*)");
    if (path.isEmpty()) return;

    fishRefAudioPath_ = path;
    const QFileInfo fi(path);
    QString label = fi.fileName();
    if (path.endsWith(".wav", Qt::CaseInsensitive)) {
        tts::AudioBuffer tmp;
        if (tts::readWavToMono(path.toStdString(), &tmp) && tmp.sampleRate > 0) {
            const double secs = static_cast<double>(tmp.samples.size()) / tmp.sampleRate;
            label += QString("  (%1 s)").arg(secs, 0, 'f', 1);
        }
    }
    fishRefAudioLabel_->setText(label);
    fishRefAudioLabel_->setStyleSheet("color: black; font-style: normal;");
    fishClearBtn_->setEnabled(true);
    setStatus("Reference audio loaded: " + fi.fileName() +
              " — select a voice slot above to use it, or save it to a slot.");
}

void MainWindow::onFishClearClicked() {
    fishRefAudioPath_.clear();
    fishRefAudioLabel_->setText("No file loaded  →  random voice will be used");
    fishRefAudioLabel_->setStyleSheet("color: gray; font-style: italic;");
    fishClearBtn_->setEnabled(false);
    setStatus("Reference audio cleared — random voice mode active.");
}

void MainWindow::onFishSaveSlotClicked() {
    if (fishRefAudioPath_.isEmpty()) {
        QMessageBox::warning(this, "No Reference Audio",
                             "Browse to a reference WAV first, then save to a slot.");
        return;
    }
    const int slot = fishSlotCombo_->currentIndex(); // 0-based
    const QString voiceDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/fish_voices";
    QDir().mkpath(voiceDir);

    const QString destPath = voiceDir + QString("/slot_%1.wav").arg(slot + 1);
    QFile::remove(destPath);
    if (!QFile::copy(fishRefAudioPath_, destPath)) {
        QMessageBox::critical(this, "Save Failed",
                              "Could not copy file to " + destPath);
        return;
    }

    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue(QString("fishVoiceSlot_%1_path").arg(slot), destPath);

    // Refresh slot labels in both the voice combo and fishSlotCombo_
    if (static_cast<tts::Provider>(providerCombo_->currentIndex()) == tts::Provider::FishSpeech) {
        updateFishVoiceComboLabels();
    }
    setStatus(QString("Reference audio saved to Slot %1. "
                      "Select 'Voice Slot %1' in the Voice combo to use it.").arg(slot + 1));
}

void MainWindow::onFishExtractTokensClicked() {
    if (fishRefAudioPath_.isEmpty()) {
        QMessageBox::warning(this, "No Reference Audio",
                             "Browse to a reference WAV first.");
        return;
    }
    const QString url = remoteGpuPanel_->fishUrl();
    if (url.isEmpty()) {
        QMessageBox::warning(this, "No Server URL",
                             "Enter the Fish Audio S2 Kaggle tunnel URL first.");
        return;
    }
    const int slot = fishSlotCombo_->currentIndex();

    fishExtractTokensBtn_->setEnabled(false);
    setStatus("Extracting VQ tokens...");

    // Read and base64-encode the reference WAV on the UI thread (small file).
    QFile f(fishRefAudioPath_);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus("Cannot read reference audio file.");
        fishExtractTokensBtn_->setEnabled(true);
        return;
    }
    const std::string b64 = f.readAll().toBase64().toStdString();
    const std::string urlStd = url.toStdString();

    const QString voiceDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/fish_voices";
    QDir().mkpath(voiceDir);
    const QString tokenPath = voiceDir + QString("/slot_%1_tokens.json").arg(slot + 1);

    fishTokenWatcher_->setFuture(QtConcurrent::run([urlStd, b64, tokenPath]() -> QString {
        try {
            tts::RemoteFishSpeechEngine engine(urlStd);
            const std::string json = engine.extractTokens(b64);
            QFile out(tokenPath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return "Cannot write token file: " + tokenPath;
            }
            out.write(json.data(), static_cast<qint64>(json.size()));
            return QString(); // success
        } catch (const std::exception& e) {
            return QString::fromStdString(e.what());
        }
    }));
}

void MainWindow::onFishExtractTokensFinished() {
    fishExtractTokensBtn_->setEnabled(true);
    const QString err = fishTokenWatcher_->result();
    if (!err.isEmpty()) {
        setStatus("Token extraction failed: " + err);
        QMessageBox::warning(this, "Token Extraction Failed", err);
    } else {
        const int slot = fishSlotCombo_->currentIndex();
        setStatus(QString("VQ tokens extracted by Kaggle and saved locally for Slot %1. "
                          "The JSON file is stored in AppData/EdgeTTS-Studio/fish_voices/.").arg(slot + 1));
    }
}

void MainWindow::onStopFishKaggleClicked() {
    const QString url = remoteGpuPanel_->fishUrl();
    if (url.isEmpty()) {
        QMessageBox::information(this, "Stop Fish Session", "Enter the Fish Audio tunnel URL first.");
        return;
    }
    setStatus("Stopping Fish Audio S2 session...");
    const std::string urlStd = url.toStdString();
    stopFishWatcher_->setFuture(
        QtConcurrent::run([urlStd]() { return tts::RemoteFishSpeechEngine::stopSession(urlStd); }));
}

void MainWindow::onStopFishKaggleFinished() {
    setStatus(stopFishWatcher_->result() ? "Fish Audio session stopped." : "Ready");
}

bool MainWindow::decodeAudioFile(const QString& path, tts::AudioBuffer* out) {
    if (path.endsWith(".wav", Qt::CaseInsensitive)) {
        return tts::readWavToMono(path.toStdString(), out);
    }

    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(path));

    std::vector<float> samples;
    int sampleRate = 0;
    bool error = false;

    QEventLoop loop;
    connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]() {
        const QAudioBuffer buf = decoder.read();
        if (!buf.isValid()) return;
        const QAudioFormat fmt = buf.format();
        sampleRate = fmt.sampleRate();
        const int channels = fmt.channelCount();
        const int frameCount = buf.frameCount();
        if (channels <= 0) return;

        switch (fmt.sampleFormat()) {
            case QAudioFormat::UInt8: {
                const auto* data = buf.constData<uint8_t>();
                for (int i = 0; i < frameCount; ++i) {
                    float sum = 0.0f;
                    for (int c = 0; c < channels; ++c) {
                        sum += (static_cast<int>(data[i * channels + c]) - 128) / 128.0f;
                    }
                    samples.push_back(sum / channels);
                }
                break;
            }
            case QAudioFormat::Int16: {
                const auto* data = buf.constData<int16_t>();
                for (int i = 0; i < frameCount; ++i) {
                    float sum = 0.0f;
                    for (int c = 0; c < channels; ++c) sum += data[i * channels + c] / 32768.0f;
                    samples.push_back(sum / channels);
                }
                break;
            }
            case QAudioFormat::Int32: {
                const auto* data = buf.constData<int32_t>();
                for (int i = 0; i < frameCount; ++i) {
                    float sum = 0.0f;
                    for (int c = 0; c < channels; ++c) {
                        sum += static_cast<float>(data[i * channels + c] / 2147483648.0);
                    }
                    samples.push_back(sum / channels);
                }
                break;
            }
            case QAudioFormat::Float: {
                const auto* data = buf.constData<float>();
                for (int i = 0; i < frameCount; ++i) {
                    float sum = 0.0f;
                    for (int c = 0; c < channels; ++c) sum += data[i * channels + c];
                    samples.push_back(sum / channels);
                }
                break;
            }
            default:
                break;
        }
    });
    connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), &decoder,
            [&](QAudioDecoder::Error) {
                error = true;
                loop.quit();
            });

    decoder.start();
    loop.exec();

    if (error || samples.empty() || sampleRate <= 0) {
        return false;
    }

    out->samples = std::move(samples);
    out->sampleRate = sampleRate;
    return true;
}

void MainWindow::onEnhanceClicked() {
    QMessageBox box(this);
    box.setWindowTitle("Enhance Audio");
    box.setText("Run DeepFilterNet noise reduction (80% denoised / 20% original blend) on:");
    QPushButton* currentBtn = box.addButton("Generated Audio", QMessageBox::AcceptRole);
    QPushButton* importBtn = box.addButton("Import File...", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    tts::AudioBuffer input;
    if (box.clickedButton() == currentBtn) {
        if (rawAudio_.samples.empty()) {
            QMessageBox::warning(this, "No audio", "Generate audio first, or use Import File... instead.");
            return;
        }
        input = rawAudio_;
    } else if (box.clickedButton() == importBtn) {
        const QString path = QFileDialog::getOpenFileName(this, "Import Audio", QString(),
                                                            "Audio Files (*.wav *.mp3 *.flac *.ogg *.m4a)");
        if (path.isEmpty()) {
            return;
        }
        if (!decodeAudioFile(path, &input)) {
            QMessageBox::critical(this, "Import failed", "Could not read audio file: " + path);
            return;
        }
    } else {
        return;
    }

    enhanceBtn_->setEnabled(false);
    playBtn_->setEnabled(false);
    exportBtn_->setEnabled(false);
    multiPanel_->setPlaybackEnabled(false);
    setStatus("Enhancing audio (DeepFilterNet)...");
    multiPanel_->setStatus("Enhancing audio (DeepFilterNet)...");

    auto future = QtConcurrent::run([this, input]() { return runEnhance(input); });
    enhanceWatcher_->setFuture(future);
}

tts::AudioBuffer MainWindow::runEnhance(const tts::AudioBuffer& input) {
    if (!dfnEngine_) {
        dfnEngine_ = std::make_shared<tts::DeepFilterNetEngine>(dfnModelDir_.toStdString());
    }

    std::vector<float> enhanced = dfnEngine_->enhance(input.samples, input.sampleRate);

    std::vector<float> blended(input.samples.size());
    for (size_t i = 0; i < blended.size(); ++i) {
        blended[i] = 0.8f * enhanced[i] + 0.2f * input.samples[i];
    }
    return tts::AudioBuffer{std::move(blended), input.sampleRate};
}

void MainWindow::onEnhanceFinished() {
    enhanceBtn_->setEnabled(true);
    try {
        rawAudio_ = enhanceWatcher_->result();
    } catch (const std::exception& e) {
        setStatus("Enhancement failed");
        multiPanel_->setStatus("Enhancement failed");
        QMessageBox::critical(this, "Enhancement error", e.what());
        return;
    }

    reprocessAndRefreshPlayback();
    playBtn_->setEnabled(true);
    exportBtn_->setEnabled(true);
    multiPanel_->setPlaybackEnabled(true);
    setStatus("Enhancement complete");
    multiPanel_->setStatus("Enhancement complete");
}

int MainWindow::voiceIndexForShortName(tts::Provider provider, const QString& shortName) const {
    const auto& voices = tts::voicesForProvider(provider);
    for (int i = 0; i < static_cast<int>(voices.size()); ++i) {
        if (QString::fromStdString(voices[static_cast<size_t>(i)].shortName) == shortName) {
            return i;
        }
    }
    return 0;
}

void MainWindow::refreshVoicePresetCombo() {
    const int previous = voicePresetCombo_->currentIndex();
    voicePresetCombo_->blockSignals(true);
    voicePresetCombo_->clear();
    voicePresetCombo_->addItem("(Select a preset)");
    for (const VoicePreset& preset : voicePresets_) {
        voicePresetCombo_->addItem(preset.name);
    }
    voicePresetCombo_->setCurrentIndex(std::clamp(previous, 0, voicePresetCombo_->count() - 1));
    voicePresetCombo_->blockSignals(false);
    deleteVoicePresetBtn_->setEnabled(voicePresetCombo_->currentIndex() > 0);
}

void MainWindow::onVoicePresetChanged(int index) {
    deleteVoicePresetBtn_->setEnabled(index > 0);
    if (index <= 0 || index - 1 >= static_cast<int>(voicePresets_.size())) {
        return;
    }

    const VoicePreset& preset = voicePresets_[static_cast<size_t>(index - 1)];
    providerCombo_->setCurrentIndex(static_cast<int>(preset.provider));
    populateVoiceCombos();
    selectVoiceAByFullIndex(voiceIndexForShortName(preset.provider, preset.voiceAShort));
    mixCheckBox_->setChecked(preset.mixEnabled);
    selectVoiceBByFullIndex(voiceIndexForShortName(preset.provider, preset.voiceBShort));
    mixSlider_->setValue(preset.pctA);
    speedSlider_->setValue(static_cast<int>(preset.speed * 100.0f));
    eqPanel_->setGainsDb(preset.eqGainsDb);
    updateMixUiEnabled();
}

void MainWindow::onSaveVoicePreset() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save Voice Preset", "Preset name:", QLineEdit::Normal,
                                                QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const auto& voices = tts::voicesForProvider(provider);
    const int idxA = comboVoiceFullIndex(voiceCombo_);
    const int idxB = comboVoiceFullIndex(voiceBCombo_);

    VoicePreset preset;
    preset.name = name.trimmed();
    preset.provider = provider;
    preset.voiceAShort = (idxA >= 0 && idxA < static_cast<int>(voices.size()))
                             ? QString::fromStdString(voices[static_cast<size_t>(idxA)].shortName)
                             : QString();
    preset.voiceBShort = (idxB >= 0 && idxB < static_cast<int>(voices.size()))
                             ? QString::fromStdString(voices[static_cast<size_t>(idxB)].shortName)
                             : QString();
    preset.mixEnabled = mixCheckBox_->isChecked();
    preset.pctA = mixSlider_->value();
    preset.speed = speedSlider_->value() / 100.0f;
    preset.eqGainsDb = eqPanel_->getGainsDb();

    for (auto& existing : voicePresets_) {
        if (existing.name == preset.name) {
            existing = preset;
            ProjectManager::saveVoicePresets(voicePresets_);
            refreshVoicePresetCombo();
            voicePresetCombo_->setCurrentText(preset.name);
            return;
        }
    }

    voicePresets_.push_back(preset);
    ProjectManager::saveVoicePresets(voicePresets_);
    refreshVoicePresetCombo();
    voicePresetCombo_->setCurrentText(preset.name);
}

void MainWindow::onDeleteVoicePreset() {
    const int index = voicePresetCombo_->currentIndex();
    if (index <= 0 || index - 1 >= static_cast<int>(voicePresets_.size())) {
        return;
    }
    voicePresets_.erase(voicePresets_.begin() + (index - 1));
    ProjectManager::saveVoicePresets(voicePresets_);
    refreshVoicePresetCombo();
}

ProjectData MainWindow::captureProjectData() const {
    ProjectData data;
    if (tabWidget_) {
        data.activeTab = tabWidget_->currentIndex();
    }

    data.singleText = textEdit_->toPlainText();
    data.singleProvider = providerCombo_->currentIndex();
    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const auto& voices = tts::voicesForProvider(provider);
    const int idxA = comboVoiceFullIndex(voiceCombo_);
    const int idxB = comboVoiceFullIndex(voiceBCombo_);
    data.singleVoiceAShort =
        (idxA >= 0 && idxA < static_cast<int>(voices.size()))
            ? QString::fromStdString(voices[static_cast<size_t>(idxA)].shortName)
            : QString();
    data.singleVoiceBShort =
        (idxB >= 0 && idxB < static_cast<int>(voices.size()))
            ? QString::fromStdString(voices[static_cast<size_t>(idxB)].shortName)
            : QString();
    data.singleMixEnabled = mixCheckBox_->isChecked();
    data.singlePctA = mixSlider_->value();
    data.singleSpeed = speedSlider_->value() / 100.0f;
    data.singleEqGainsDb = eqPanel_->getGainsDb();
    data.humanizerEnabled = humanizerCheckBox_->isChecked();

    const QJsonObject multi = multiPanel_->toJson();
    data.multiScript = multi["script"].toString();
    data.multiSpeakerCount = multi["speakerCount"].toInt(2);
    data.multiCurrentSpeaker = multi["currentSpeaker"].toInt(0);
    data.multiPauseMs = multi["pauseMs"].toInt(220);
    data.multiSpeakerSettings = multi["speakerSettings"].toArray();

    data.useGpu = gpuCheckBox_->isChecked();
    data.useRemoteKokoro = remoteGpuPanel_->kokoroEnabled();
    data.remoteKokoroUrl = remoteGpuPanel_->kokoroUrl();
    data.useRemoteFish = remoteGpuPanel_->fishEnabled();
    data.remoteFishUrl = remoteGpuPanel_->fishUrl();
    data.maxChunkChars = maxChunkChars_;
    data.sentenceGapMs = sentenceGapMs_;
    data.paragraphGapMs = paragraphGapMs_;

    if (timelinePanel_) {
        data.timeline = timelinePanel_->segments();
    }
    if (pronunciationPanel_) {
        data.pronunciationDictionary = pronunciationPanel_->dictionaryToJson();
    }
    return data;
}

void MainWindow::applyProjectData(const ProjectData& data) {
    textEdit_->setPlainText(data.singleText);
    providerCombo_->setCurrentIndex(data.singleProvider);
    populateVoiceCombos();
    selectVoiceAByFullIndex(
        voiceIndexForShortName(static_cast<tts::Provider>(data.singleProvider), data.singleVoiceAShort));
    mixCheckBox_->setChecked(data.singleMixEnabled);
    selectVoiceBByFullIndex(
        voiceIndexForShortName(static_cast<tts::Provider>(data.singleProvider), data.singleVoiceBShort));
    mixSlider_->setValue(data.singlePctA);
    speedSlider_->setValue(static_cast<int>(data.singleSpeed * 100.0f));
    eqPanel_->setGainsDb(data.singleEqGainsDb);
    humanizerCheckBox_->setChecked(data.humanizerEnabled);
    updateMixUiEnabled();

    QJsonObject multi;
    multi["script"] = data.multiScript;
    multi["speakerCount"] = data.multiSpeakerCount;
    multi["currentSpeaker"] = data.multiCurrentSpeaker;
    multi["pauseMs"] = data.multiPauseMs;
    multi["speakerSettings"] = data.multiSpeakerSettings;
    multiPanel_->fromJson(multi);

    gpuCheckBox_->setChecked(data.useGpu);
    remoteGpuPanel_->loadFromSettings();
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue("useRemoteKokoro", data.useRemoteKokoro);
    settings.setValue("remoteKokoroUrl", data.remoteKokoroUrl);
    settings.setValue("useRemoteFish", data.useRemoteFish);
    settings.setValue("remoteFishUrl", data.remoteFishUrl);
    remoteGpuPanel_->loadFromSettings();
    maxChunkChars_ = data.maxChunkChars;
    sentenceGapMs_ = data.sentenceGapMs;
    paragraphGapMs_ = data.paragraphGapMs;

    if (tabWidget_) {
        tabWidget_->setCurrentIndex(data.activeTab);
    }
    if (pronunciationPanel_) {
        pronunciationPanel_->dictionaryFromJson(data.pronunciationDictionary);
    }

    setTimelineSegments(data.timeline);
    if (!data.timeline.empty()) {
        rebuildAudioFromTimeline();
    } else {
        rawAudio_ = {};
        processedAudio_.clear();
        playBtn_->setEnabled(false);
        exportBtn_->setEnabled(false);
        multiPanel_->setPlaybackEnabled(false);
    }

    multiPanel_->setChunkSettings(maxChunkChars_, sentenceGapMs_, paragraphGapMs_);
    textStats_->updateStats(textEdit_->toPlainText(), speedSlider_->value() / 100.0f, maxChunkChars_,
                            sentenceGapMs_, paragraphGapMs_);
    updateStatusBar();
}

void MainWindow::updateWindowTitle() {
    QString title = "EdgeTTS-Studio Native";
    if (!currentProjectPath_.isEmpty()) {
        title += " — " + QFileInfo(currentProjectPath_).fileName();
        if (projectDirty_) {
            title += " *";
        }
    }
    setWindowTitle(title);
}

void MainWindow::onNewProject() {
    currentProjectPath_.clear();
    projectDirty_ = false;
    updateStatusBar();
    textEdit_->setPlainText("");
    multiPanel_->resetToDefaults();
    setTimelineSegments({});
    rawAudio_ = {};
    processedAudio_.clear();
    playBtn_->setEnabled(false);
    exportBtn_->setEnabled(false);
    multiPanel_->setPlaybackEnabled(false);
    updateWindowTitle();
}

void MainWindow::onOpenProject() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Project", QString(), "EdgeTTS Project (*.edgettsproj);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }

    ProjectData data;
    QString error;
    if (!ProjectManager::loadProject(path, &data, &error)) {
        QMessageBox::critical(this, "Open Project", error.isEmpty() ? "Could not open project." : error);
        return;
    }

    applyProjectData(data);
    currentProjectPath_ = path;
    projectDirty_ = false;
    ui::RecentProjects::add(path);
    refreshRecentProjectsMenu();
    updateWindowTitle();
    updateStatusBar();
    setStatus("Project loaded.");
}

void MainWindow::onSaveProject() {
    if (currentProjectPath_.isEmpty()) {
        onSaveProjectAs();
        return;
    }
    if (!ProjectManager::saveProject(currentProjectPath_, captureProjectData())) {
        QMessageBox::critical(this, "Save Project", "Could not save project file.");
        return;
    }
    projectDirty_ = false;
    ui::RecentProjects::add(currentProjectPath_);
    refreshRecentProjectsMenu();
    updateWindowTitle();
    updateStatusBar();
    setStatus("Project saved.");
}

void MainWindow::onSaveProjectAs() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Project As", QString(), "EdgeTTS Project (*.edgettsproj);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    QString finalPath = path;
    if (!finalPath.endsWith(".edgettsproj", Qt::CaseInsensitive)) {
        finalPath += ".edgettsproj";
    }
    if (!ProjectManager::saveProject(finalPath, captureProjectData())) {
        QMessageBox::critical(this, "Save Project", "Could not save project file.");
        return;
    }
    currentProjectPath_ = finalPath;
    projectDirty_ = false;
    ui::RecentProjects::add(finalPath);
    refreshRecentProjectsMenu();
    updateWindowTitle();
    updateStatusBar();
    setStatus("Project saved.");
}

void MainWindow::setTimelineSegments(const std::vector<TimelineSegment>& segments) {
    if (timelinePanel_) {
        timelinePanel_->setSegments(segments);
        timelinePanel_->setStatus(segments.empty()
                                      ? "Synthesize or render dialogue to populate the timeline."
                                      : QString("%1 segment(s) loaded.").arg(static_cast<int>(segments.size())));
    }
}

void MainWindow::rebuildAudioFromTimeline() {
    if (!timelinePanel_) {
        return;
    }
    rawAudio_ = timeline::buildFromTimeline(timelinePanel_->segments());
    reprocessAndRefreshPlayback();
    const bool hasAudio = !processedAudio_.empty();
    playBtn_->setEnabled(hasAudio);
    exportBtn_->setEnabled(hasAudio);
    multiPanel_->setPlaybackEnabled(hasAudio);
    markProjectDirty();
    updateStatusBar();

    const double seconds = rawAudio_.sampleRate > 0
                               ? static_cast<double>(rawAudio_.samples.size()) / rawAudio_.sampleRate
                               : 0.0;
    timelinePanel_->setStatus(QString("Timeline applied (%1s audio).").arg(seconds, 0, 'f', 1));
}

void MainWindow::onTimelineRebuildRequested() {
    rebuildAudioFromTimeline();
    setStatus("Timeline changes applied to audio.");
}

void MainWindow::onPronunciationPreviewRequested() {
    if (pronunciationPanel_) {
        pronunciationPanel_->showPreviewFor(textEdit_->toPlainText());
    }
}

void MainWindow::onExportPackage() {
    if (!timelinePanel_ || timelinePanel_->segments().empty()) {
        QMessageBox::warning(this, "Export Package",
                             "Synthesize or render dialogue first so the timeline has segments to export.");
        return;
    }

    ExportPackageDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString folder = QFileDialog::getExistingDirectory(this, "Choose Export Package Folder");
    if (folder.isEmpty()) {
        return;
    }

    const captions::ExportPackageResult result = captions::exportPackage(
        folder, timelinePanel_->segments(), processedAudio_, rawAudio_.sampleRate, dialog.options());

    if (result.success) {
        setStatus(result.message);
        multiPanel_->setStatus(result.message);
        QMessageBox::information(this, "Export Package", result.message);
    } else {
        QMessageBox::critical(this, "Export Package", result.message);
    }
}

void MainWindow::refreshRecentProjectsMenu() {
    if (!recentProjectsMenu_) {
        return;
    }
    recentProjectsMenu_->clear();
    const QStringList items = ui::RecentProjects::list();
    if (items.isEmpty()) {
        QAction* empty = recentProjectsMenu_->addAction("(No recent projects)");
        empty->setEnabled(false);
        return;
    }
    for (const QString& path : items) {
        QAction* action = recentProjectsMenu_->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        action->setData(path);
        connect(action, &QAction::triggered, this, &MainWindow::onOpenRecentProject);
    }
    recentProjectsMenu_->addSeparator();
    QAction* clear = recentProjectsMenu_->addAction("Clear Recent");
    connect(clear, &QAction::triggered, this, [this]() {
        QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
        settings.setValue("recentProjects", QStringList{});
        refreshRecentProjectsMenu();
    });
}

void MainWindow::onOpenRecentProject() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }
    const QString path = action->data().toString();
    if (path.isEmpty()) {
        return;
    }
    ProjectData data;
    QString error;
    if (!ProjectManager::loadProject(path, &data, &error)) {
        QMessageBox::critical(this, "Open Project", error.isEmpty() ? "Could not open project." : error);
        ui::RecentProjects::remove(path);
        refreshRecentProjectsMenu();
        return;
    }
    applyProjectData(data);
    currentProjectPath_ = path;
    projectDirty_ = false;
    ui::RecentProjects::add(path);
    refreshRecentProjectsMenu();
    updateWindowTitle();
    updateStatusBar();
    setStatus("Project loaded.");
}

void MainWindow::onCancelSynthesisClicked() {
    synthesisCancel_.store(true);
    setStatus("Cancelling synthesis...");
    multiPanel_->setStatus("Cancelling...");
}

void MainWindow::onPreviewSelectionClicked() {
    QString text = textEdit_->textCursor().selectedText();
    text.replace(QChar::ParagraphSeparator, '\n');
    if (text.trimmed().isEmpty()) {
        text = textEdit_->toPlainText();
    }
    if (text.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Preview", "Select some text or enter text to preview.");
        return;
    }
    if (!supertonicEngine_ && static_cast<tts::Provider>(providerCombo_->currentIndex()) ==
                                  tts::Provider::Supertonic) {
        QMessageBox::warning(this, "Not ready", "Supertonic models are still loading.");
        return;
    }

    SynthRequest req;
    req.text = text.toStdString();
    req.speed = speedSlider_->value() / 100.0f;
    req.provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const auto& voices = tts::voicesForProvider(req.provider);
    const int idxA = comboVoiceFullIndex(voiceCombo_);
    if (idxA < 0 || idxA >= static_cast<int>(voices.size())) {
        return;
    }
    req.voiceA = voices[static_cast<size_t>(idxA)];
    req.mixEnabled = tts::supportsVoiceMixing(req.provider) && mixCheckBox_->isChecked();
    if (req.mixEnabled) {
        const int idxB = comboVoiceFullIndex(voiceBCombo_);
        if (idxB >= 0 && idxB < static_cast<int>(voices.size())) {
            req.voiceB = voices[static_cast<size_t>(idxB)];
            req.pctA = mixSlider_->value();
        } else {
            req.mixEnabled = false;
        }
    }
    runPreviewSynth(req, previewBtn_);
}

void MainWindow::onPreviewFinished() {
    if (previewDisableBtn_) {
        previewDisableBtn_->setEnabled(true);
        previewDisableBtn_ = nullptr;
    }
    try {
        const SynthOutput output = previewWatcher_->result();
        if (!output.audio.samples.empty()) {
            std::vector<float> preview = output.audio.samples;
            tts::GraphicEq eq;
            eq.setGainsDb(eqPanel_->getGainsDb(), static_cast<float>(output.audio.sampleRate));
            eq.process(preview);
            tts::peakNormalize(preview);
            playPreviewSamples(preview, output.audio.sampleRate);
            setStatus(QString("Preview playing (%1s)...")
                          .arg(static_cast<double>(preview.size()) / output.audio.sampleRate, 0, 'f', 1));
        }
    } catch (const std::exception& e) {
        setStatus(QString("Preview failed: %1").arg(e.what()));
        QMessageBox::warning(this, "Preview failed", e.what());
    }
}

void MainWindow::playPreviewSamples(const std::vector<float>& samples, int sampleRate) {
    onStopClicked();
    if (samples.empty()) {
        return;
    }
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    audioSink_ = new QAudioSink(format, this);
    std::vector<int16_t> pcm = toInt16(samples);
    playbackBytes_ = QByteArray(reinterpret_cast<const char*>(pcm.data()),
                                 static_cast<int>(pcm.size() * sizeof(int16_t)));
    playbackBuffer_.close();
    playbackBuffer_.setBuffer(&playbackBytes_);
    playbackBuffer_.open(QIODevice::ReadOnly);
    audioSink_->start(&playbackBuffer_);
}

void MainWindow::onPlaybackTick() {
    if (!audioSink_ || audioSink_->state() == QtAudio::StoppedState) {
        if (playbackTimer_) {
            playbackTimer_->stop();
        }
        if (timelinePanel_) {
            timelinePanel_->setPlaybackPositionSeconds(-1.0);
        }
        return;
    }
    const qint64 usec = audioSink_->processedUSecs();
    const double seconds = playbackStartOffsetSec_ + static_cast<double>(usec) / 1'000'000.0;
    if (timelinePanel_) {
        timelinePanel_->setPlaybackPositionSeconds(seconds);
    }
}

void MainWindow::onToggleDarkMode() {
    ui::ThemeManager::toggle();
}

void MainWindow::onModelManagerClicked() {
    ModelManagerDialog dialog(this);
    dialog.exec();
}

void MainWindow::onBatchQueueClicked() {
    batchQueueDialog_->show();
    batchQueueDialog_->raise();
    batchQueueDialog_->activateWindow();
}

void MainWindow::onBatchItemRequested(const QString& text, const QString& outputPath, int itemIndex) {
    if (batchQueueDialog_->isCancelRequested()) {
        batchQueueDialog_->onItemFinished(itemIndex, false, "Cancelled.");
        return;
    }

    SynthRequest req;
    req.text = text.toStdString();
    req.speed = speedSlider_->value() / 100.0f;
    req.provider = static_cast<tts::Provider>(providerCombo_->currentIndex());

    if (req.provider == tts::Provider::Supertonic && !supertonicEngine_) {
        batchQueueDialog_->onItemFinished(itemIndex, false, "Supertonic models are still loading.");
        return;
    }

    const auto& voices = tts::voicesForProvider(req.provider);
    const int idxA = comboVoiceFullIndex(voiceCombo_);
    if (idxA < 0 || idxA >= static_cast<int>(voices.size())) {
        batchQueueDialog_->onItemFinished(itemIndex, false, "No voice selected.");
        return;
    }
    req.voiceA = voices[idxA];
    req.mixEnabled = false;
    captureRemoteGpuState();

    QString validationError;
    if (!validateCurrentProvider(&req.voiceA, &validationError)) {
        batchQueueDialog_->onItemFinished(itemIndex, false, validationError);
        return;
    }

    humanizerEnabled_ = humanizerCheckBox_->isChecked();
    req.dictionary =
        pronunciationPanel_ ? pronunciationPanel_->dictionary() : std::vector<tts::PronunciationEntry>{};

    pendingBatchItemIndex_ = itemIndex;
    pendingExportPath_ = outputPath;
    batchQueueDialog_->setBusy(true);
    synthesisCancel_.store(false);

    const QString format = batchQueueDialog_->outputFormat();
    tts::AudioExportFormat exportFormat = tts::AudioExportFormat::Wav;
    if (format.compare("MP3", Qt::CaseInsensitive) == 0) {
        exportFormat = tts::AudioExportFormat::Mp3;
    } else if (format.compare("FLAC", Qt::CaseInsensitive) == 0) {
        exportFormat = tts::AudioExportFormat::Flac;
    }

    auto batchError = std::make_shared<QString>();
    auto future = QtConcurrent::run([this, req, outputPath, exportFormat, batchError]() -> bool {
        try {
            const SynthOutput output = runSynthesis(req);
            std::vector<float> samples = output.audio.samples;
            tts::GraphicEq eq;
            eq.setGainsDb(eqPanel_->getGainsDb(), static_cast<float>(output.audio.sampleRate));
            eq.process(samples);
            tts::peakNormalize(samples);
            return tts::exportAudio(outputPath.toStdString(), exportFormat, samples,
                                    static_cast<uint32_t>(output.audio.sampleRate));
        } catch (const std::exception& e) {
            *batchError = QString::fromUtf8(e.what());
            return false;
        }
    });
    pendingBatchError_.clear();
    pendingBatchErrorPtr_ = batchError;
    batchExportWatcher_->setFuture(future);
}

void MainWindow::onBatchCancelRequested() {
    synthesisCancel_.store(true);
}

void MainWindow::onBatchItemExportFinished() {
    const bool ok = batchExportWatcher_->result();
    batchQueueDialog_->setBusy(false);
    if (pendingBatchItemIndex_ >= 0) {
        const QString err = (!ok && pendingBatchErrorPtr_) ? *pendingBatchErrorPtr_ : QString();
        batchQueueDialog_->onItemFinished(pendingBatchItemIndex_, ok, err);
        pendingBatchItemIndex_ = -1;
        pendingBatchErrorPtr_.reset();
    }
}

void MainWindow::onPartialPlaybackReady() {
    if (partialPlaybackStarted_ || partialPlaybackSamples_.empty()) {
        return;
    }
    partialPlaybackStarted_ = true;
    playPreviewSamples(partialPlaybackSamples_, partialPlaybackSampleRate_);
    setStatus("Streaming playback started (first chunk ready)...");
}

void MainWindow::onTimelineRerenderRequested(int segmentIndex, const QString& text) {
    if (text.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Re-render Segment", "Selected segment has no text.");
        return;
    }

    SynthRequest req;
    req.text = text.toStdString();
    req.speed = speedSlider_->value() / 100.0f;
    req.provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const auto& voices = tts::voicesForProvider(req.provider);
    const int idxA = comboVoiceFullIndex(voiceCombo_);
    if (idxA < 0 || idxA >= static_cast<int>(voices.size())) {
        return;
    }
    req.voiceA = voices[idxA];
    req.mixEnabled = tts::supportsVoiceMixing(req.provider) && mixCheckBox_->isChecked();
    if (req.mixEnabled) {
        const int idxB = comboVoiceFullIndex(voiceBCombo_);
        if (idxB >= 0 && idxB < static_cast<int>(voices.size())) {
            req.voiceB = voices[idxB];
            req.pctA = mixSlider_->value();
        } else {
            req.mixEnabled = false;
        }
    }

    captureRemoteGpuState();
    QString validationError;
    if (!validateCurrentProvider(&req.voiceA, &validationError)) {
        QMessageBox::warning(this, "Not ready", validationError);
        return;
    }

    humanizerEnabled_ = humanizerCheckBox_->isChecked();
    pendingRerenderSegmentIndex_ = segmentIndex;
    timelinePanel_->setStatus("Re-rendering selected segment...");

    auto future = QtConcurrent::run([this, req]() -> tts::AudioBuffer {
        tts::AudioBuffer buf = synthesizeSegmentMixed(req.provider, req.voiceA, req.mixEnabled, req.voiceB, req.pctA,
                                                       req.text, req.speed);
        if (humanizerEnabled_ && req.provider == tts::Provider::EdgeTts) {
            tts::applyHumanizer(buf, humanizerSettings_);
        }
        constexpr int kTargetRate = 44100;
        buf.samples = tts::resampleLinear(buf.samples, buf.sampleRate, kTargetRate);
        buf.sampleRate = kTargetRate;
        tts::GraphicEq eq;
        eq.setGainsDb(eqPanel_->getGainsDb(), static_cast<float>(buf.sampleRate));
        eq.process(buf.samples);
        tts::peakNormalize(buf.samples);
        return buf;
    });
    segmentRerenderWatcher_->setFuture(future);
}

void MainWindow::onTimelineRerenderFinished() {
    if (pendingRerenderSegmentIndex_ < 0) {
        return;
    }
    try {
        const tts::AudioBuffer buf = segmentRerenderWatcher_->result();
        auto segments = timelinePanel_->segments();
        if (pendingRerenderSegmentIndex_ >= 0 &&
            pendingRerenderSegmentIndex_ < static_cast<int>(segments.size())) {
            segments[static_cast<size_t>(pendingRerenderSegmentIndex_)].samples = buf.samples;
            segments[static_cast<size_t>(pendingRerenderSegmentIndex_)].sampleRate = buf.sampleRate;
            setTimelineSegments(segments);
            rebuildAudioFromTimeline();
            markProjectDirty();
            timelinePanel_->setStatus("Segment re-rendered.");
        }
    } catch (const std::exception& e) {
        timelinePanel_->setStatus("Re-render failed.");
        QMessageBox::critical(this, "Re-render error", e.what());
    }
    pendingRerenderSegmentIndex_ = -1;
}

void MainWindow::onTestEdgeTtsRequested() {
    if (edgeTestWatcher_->isRunning()) {
        return;
    }
    edgeTestHandled_ = false;
    remoteGpuPanel_->setEdgeTestInProgress(true);

    edgeTestWatcher_->setFuture(QtConcurrent::run([]() {
        tts::EdgeTtsEngine engine;
        return engine.testConnection();
    }));

    QTimer::singleShot(30000, this, [this]() {
        if (edgeTestHandled_ || !edgeTestWatcher_->isRunning()) {
            return;
        }
        edgeTestHandled_ = true;
        remoteGpuPanel_->setEdgeTestResult(false, "Timed out after 30 seconds");
        setStatus("Edge TTS test timed out");
    });
}

void MainWindow::onTestEdgeTtsFinished() {
    if (!edgeTestWatcher_->isFinished() || edgeTestHandled_) {
        return;
    }
    edgeTestHandled_ = true;
    try {
        const auto result = edgeTestWatcher_->result();
        remoteGpuPanel_->setEdgeTestResult(result.ok, QString::fromStdString(result.message));
        if (result.ok) {
            setStatus(QString("Edge TTS OK: %1").arg(QString::fromStdString(result.message)));
        } else {
            setStatus(QString("Edge TTS failed: %1").arg(QString::fromStdString(result.message)));
            QMessageBox::warning(this, "Edge TTS Connection Test", QString::fromStdString(result.message));
        }
    } catch (const std::exception& e) {
        remoteGpuPanel_->setEdgeTestResult(false, QString::fromUtf8(e.what()));
        setStatus(QString("Edge TTS test error: %1").arg(e.what()));
    }
}

void MainWindow::offerAutosaveRecovery() {
    const QString autosavePath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave.edgettsproj";
    if (!QFileInfo::exists(autosavePath)) {
        return;
    }
    if (!currentProjectPath_.isEmpty()) {
        const QFileInfo projectInfo(currentProjectPath_);
        const QFileInfo autosaveInfo(autosavePath);
        if (projectInfo.exists() && projectInfo.lastModified() >= autosaveInfo.lastModified()) {
            return;
        }
    }
    const auto answer = QMessageBox::question(
        this, "Recover Autosave",
        "A newer autosave was found. Restore it now?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        return;
    }
    ProjectData data;
    QString err;
    if (ProjectManager::loadProject(autosavePath, &data, &err)) {
        applyProjectData(data);
        currentProjectPath_.clear();
        projectDirty_ = true;
        updateWindowTitle();
        setStatus("Restored autosave.");
    } else {
        QMessageBox::warning(this, "Autosave Recovery", err);
    }
}

void MainWindow::showFirstRunIfNeeded() {
    if (!FirstRunWizard::shouldShow()) {
        return;
    }
    FirstRunWizard wizard(modelDir_, this);
    wizard.exec();
}
