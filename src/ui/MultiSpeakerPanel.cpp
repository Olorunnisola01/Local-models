#include "MultiSpeakerPanel.h"

#include <algorithm>

#include <QButtonGroup>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

#include "ProviderHintsPanel.h"
#include "SpeakerVoiceCard.h"
#include "TagInserter.h"
#include "TextStatsPanel.h"

namespace {
constexpr const char* kExampleScript =
    "A: Hello there! How are you doing today?\n"
    "B: I'm doing quite well, thank you for asking.\n"
    "A: That's great to hear. Did you finish the project we discussed?\n"
    "B: Almost! I just need to add the final touches and test the dialogue.\n";
}

MultiSpeakerPanel::MultiSpeakerPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* scriptHeader = new QHBoxLayout();
    scriptHeader->addWidget(new QLabel("Script (one turn per line, e.g. \"A: Hello there\"):", this));
    scriptHeader->addStretch(1);
    previewBtn_ = new QPushButton("Preview Selection", this);
    previewBtn_->setToolTip("Synthesize and play the selected script text using the current speaker's voice");
    importBtn_ = new QPushButton("Import Text...", this);
    importBtn_->setToolTip("Load a .txt file into the script editor");
    scriptHeader->addWidget(importBtn_);
    scriptHeader->addWidget(previewBtn_);
    layout->addLayout(scriptHeader);

    scriptEdit_ = new QPlainTextEdit(this);
    scriptEdit_->setPlainText(kExampleScript);
    scriptEdit_->setMinimumHeight(120);
    layout->addWidget(scriptEdit_);

    textStats_ = new TextStatsPanel(this);
    layout->addWidget(textStats_);

    tagInserter_ = new TagInserter(scriptEdit_, this);
    layout->addWidget(tagInserter_);

    providerHints_ = new ProviderHintsPanel(this);
    layout->addWidget(providerHints_);

    auto* speakerRow = new QHBoxLayout();
    speakerRow->addWidget(new QLabel("Speakers in script:", this));
    speakerCountSpin_ = new QSpinBox(this);
    speakerCountSpin_->setRange(1, kMaxSpeakers);
    speakerCountSpin_->setValue(2);
    speakerRow->addWidget(speakerCountSpin_);

    speakerRow->addSpacing(16);
    speakerRow->addWidget(new QLabel("Editing speaker:", this));
    speakerButtonGroup_ = new QButtonGroup(this);
    speakerButtonGroup_->setExclusive(true);
    for (int i = 0; i < kMaxSpeakers; ++i) {
        auto* btn = new QPushButton(QString(QChar('A' + i)), this);
        btn->setCheckable(true);
        btn->setFixedWidth(36);
        btn->setChecked(i == 0);
        speakerButtons_[i] = btn;
        speakerButtonGroup_->addButton(btn, i);
        speakerRow->addWidget(btn);
    }
    speakerRow->addStretch(1);
    layout->addLayout(speakerRow);

    speakerEditor_ = new SpeakerVoiceCard("Speaker A", this);
    layout->addWidget(speakerEditor_, 1);

    for (auto& settings : speakerSettings_) {
        settings = SpeakerSettings{};
    }
    onSpeakerCountChanged(speakerCountSpin_->value());

    auto* pauseRow = new QHBoxLayout();
    pauseRow->addWidget(new QLabel("Pause between lines (ms):", this));
    pauseSlider_ = new QSlider(Qt::Horizontal, this);
    pauseSlider_->setRange(0, 1200);
    pauseSlider_->setValue(220);
    pauseRow->addWidget(pauseSlider_);
    pauseLabel_ = new QLabel("220", this);
    pauseRow->addWidget(pauseLabel_);
    layout->addLayout(pauseRow);
    connect(pauseSlider_, &QSlider::valueChanged, this,
            [this](int v) { pauseLabel_->setText(QString::number(v)); });

    auto* buttonRow = new QHBoxLayout();
    renderBtn_ = new QPushButton("Render All", this);
    cancelBtn_ = new QPushButton("Cancel", this);
    cancelBtn_->setEnabled(false);
    playBtn_ = new QPushButton("Play", this);
    pauseBtn_ = new QPushButton("Pause", this);
    stopBtn_ = new QPushButton("Stop", this);
    exportBtn_ = new QPushButton("Export Audio...", this);
    enhanceBtn_ = new QPushButton("Enhance (DeepFilterNet)...", this);
    playBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    stopBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    exportBtn_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    playBtn_->setEnabled(false);
    pauseBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);
    exportBtn_->setEnabled(false);
    buttonRow->addWidget(renderBtn_);
    buttonRow->addWidget(cancelBtn_);
    buttonRow->addWidget(playBtn_);
    buttonRow->addWidget(pauseBtn_);
    buttonRow->addWidget(stopBtn_);
    buttonRow->addWidget(exportBtn_);
    buttonRow->addWidget(enhanceBtn_);
    layout->addLayout(buttonRow);

    statusLabel_ = new QLabel("Write a dialogue script and click Render All.", this);
    layout->addWidget(statusLabel_);

    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    layout->addWidget(progressBar_);

    connect(speakerCountSpin_, &QSpinBox::valueChanged, this, &MultiSpeakerPanel::onSpeakerCountChanged);
    connect(speakerButtonGroup_, &QButtonGroup::idClicked, this, &MultiSpeakerPanel::onSpeakerButtonClicked);
    connect(renderBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::onRenderClicked);
    connect(cancelBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::cancelRequested);
    connect(previewBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::onPreviewClicked);
    connect(importBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::onImportClicked);
    connect(playBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::playRequested);
    connect(pauseBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::pauseRequested);
    connect(stopBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::stopRequested);
    connect(exportBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::exportRequested);
    connect(enhanceBtn_, &QPushButton::clicked, this, &MultiSpeakerPanel::enhanceRequested);
    connect(scriptEdit_->document(), &QTextDocument::contentsChanged, this, &MultiSpeakerPanel::onScriptChanged);
    connect(speakerEditor_, &SpeakerVoiceCard::providerIndexChanged, this, [this](int index) {
        tagInserter_->setProviderIndex(index);
        providerHints_->setProviderIndex(index);
        onScriptChanged();
    });
    connect(speakerEditor_, &SpeakerVoiceCard::editorSettingsChanged, this, &MultiSpeakerPanel::onScriptChanged);

    tagInserter_->setProviderIndex(static_cast<int>(speakerEditor_->provider()));
    providerHints_->setProviderIndex(static_cast<int>(speakerEditor_->provider()));
    onScriptChanged();
}

QPushButton* MultiSpeakerPanel::previewButton() const {
    return previewBtn_;
}

void MultiSpeakerPanel::setChunkSettings(int maxChunkChars, int sentenceGapMs, int paragraphGapMs) {
    maxChunkChars_ = maxChunkChars;
    sentenceGapMs_ = sentenceGapMs;
    paragraphGapMs_ = paragraphGapMs;
    onScriptChanged();
}

void MultiSpeakerPanel::onScriptChanged() {
    textStats_->updateStats(scriptEdit_->toPlainText(), speakerEditor_->speed(), maxChunkChars_, sentenceGapMs_,
                            paragraphGapMs_);
}

DialogueSegment MultiSpeakerPanel::currentSpeakerLine(const QString& text) const {
    const SpeakerSettings& s = speakerSettings_[currentSpeaker_];
    const auto& voices = tts::voicesForProvider(s.provider);
    DialogueSegment seg;
    seg.provider = s.provider;
    seg.voiceA = (s.voiceAIndex >= 0 && s.voiceAIndex < static_cast<int>(voices.size()))
                     ? voices[static_cast<size_t>(s.voiceAIndex)]
                     : tts::VoiceEntry{};
    seg.mixEnabled = s.mixEnabled && tts::supportsVoiceMixing(s.provider);
    seg.voiceB = (s.voiceBIndex >= 0 && s.voiceBIndex < static_cast<int>(voices.size()))
                     ? voices[static_cast<size_t>(s.voiceBIndex)]
                     : tts::VoiceEntry{};
    seg.pctA = s.pctA;
    seg.speed = s.speed;
    seg.eqGainsDb = s.eqGainsDb;
    seg.humanizerEnabled = s.humanizerEnabled;
    seg.text = text.toStdString();
    return seg;
}

void MultiSpeakerPanel::setBusy(bool busy) {
    renderBtn_->setEnabled(!busy);
    previewBtn_->setEnabled(!busy);
    cancelBtn_->setEnabled(busy);
}

void MultiSpeakerPanel::setPlaybackEnabled(bool enabled) {
    playBtn_->setEnabled(enabled);
    pauseBtn_->setEnabled(enabled);
    if (!enabled) {
        pauseBtn_->setText("Pause");
    }
    stopBtn_->setEnabled(enabled);
    exportBtn_->setEnabled(enabled);
}

void MultiSpeakerPanel::setStatus(const QString& text) {
    statusLabel_->setText(text);
}

void MultiSpeakerPanel::setProgress(int percent) {
    progressBar_->setValue(percent);
}

void MultiSpeakerPanel::setPauseLabel(const QString& text) {
    pauseBtn_->setText(text);
}

void MultiSpeakerPanel::onPreviewClicked() {
    saveEditorToSettings(currentSpeaker_);
    QString text = scriptEdit_->textCursor().selectedText();
    text.replace(QChar::ParagraphSeparator, '\n');
    if (text.trimmed().isEmpty()) {
        text = scriptEdit_->toPlainText();
    }
    if (text.trimmed().isEmpty()) {
        return;
    }
    emit previewSelectionRequested(text);
}

void MultiSpeakerPanel::onImportClicked() {
    const QString path = QFileDialog::getOpenFileName(this, "Import Script Text", QString(),
                                                      "Text Files (*.txt);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    scriptEdit_->setPlainText(QString::fromUtf8(file.readAll()));
    emit importTextRequested();
}

void MultiSpeakerPanel::saveEditorToSettings(int index) {
    SpeakerSettings& s = speakerSettings_[index];
    s.provider = speakerEditor_->provider();
    s.voiceAIndex = speakerEditor_->voiceAIndex();
    s.mixEnabled = speakerEditor_->mixEnabled();
    s.voiceBIndex = speakerEditor_->voiceBIndex();
    s.pctA = speakerEditor_->pctA();
    s.speed = speakerEditor_->speed();
    s.eqGainsDb = speakerEditor_->eqGainsDb();
    s.humanizerEnabled = speakerEditor_->humanizerEnabled();
}

void MultiSpeakerPanel::loadSettingsToEditor(int index) {
    const SpeakerSettings& s = speakerSettings_[index];
    speakerEditor_->setTitle(QString("Speaker %1").arg(QChar('A' + index)));
    speakerEditor_->setProvider(s.provider);
    speakerEditor_->setVoiceAIndex(s.voiceAIndex);
    speakerEditor_->setMixEnabled(s.mixEnabled);
    speakerEditor_->setVoiceBIndex(s.voiceBIndex);
    speakerEditor_->setPctA(s.pctA);
    speakerEditor_->setSpeed(s.speed);
    speakerEditor_->setEqGainsDb(s.eqGainsDb);
    speakerEditor_->setHumanizerEnabled(s.humanizerEnabled);
    tagInserter_->setProviderIndex(static_cast<int>(s.provider));
    providerHints_->setProviderIndex(static_cast<int>(s.provider));
    onScriptChanged();
}

void MultiSpeakerPanel::onSpeakerCountChanged(int count) {
    for (int i = 0; i < kMaxSpeakers; ++i) {
        speakerButtons_[i]->setVisible(i < count);
    }
    if (currentSpeaker_ >= count) {
        saveEditorToSettings(currentSpeaker_);
        currentSpeaker_ = 0;
        speakerButtons_[0]->setChecked(true);
        loadSettingsToEditor(currentSpeaker_);
    }
}

void MultiSpeakerPanel::onSpeakerButtonClicked(int index) {
    if (index == currentSpeaker_) {
        return;
    }
    saveEditorToSettings(currentSpeaker_);
    currentSpeaker_ = index;
    loadSettingsToEditor(currentSpeaker_);
}

std::vector<DialogueSegment> MultiSpeakerPanel::parseScript() const {
    static const QRegularExpression lineRe(QStringLiteral("^([A-Ea-e])\\s*[:：]\\s*(.*)$"));

    std::vector<DialogueSegment> segments;
    int currentSpeaker = -1;
    QStringList currentLines;

    auto flush = [&]() {
        if (currentSpeaker >= 0 && !currentLines.isEmpty()) {
            const QString text = currentLines.join(' ').trimmed();
            if (!text.isEmpty()) {
                const SpeakerSettings& s = speakerSettings_[currentSpeaker];
                const auto& voices = tts::voicesForProvider(s.provider);
                DialogueSegment seg;
                seg.provider = s.provider;
                seg.voiceA = (s.voiceAIndex >= 0 && s.voiceAIndex < static_cast<int>(voices.size()))
                                  ? voices[s.voiceAIndex]
                                  : tts::VoiceEntry{};
                seg.mixEnabled = s.mixEnabled && tts::supportsVoiceMixing(s.provider);
                seg.voiceB = (s.voiceBIndex >= 0 && s.voiceBIndex < static_cast<int>(voices.size()))
                                  ? voices[s.voiceBIndex]
                                  : tts::VoiceEntry{};
                seg.pctA = s.pctA;
                seg.speed = s.speed;
                seg.eqGainsDb = s.eqGainsDb;
                seg.humanizerEnabled = s.humanizerEnabled;
                seg.text = text.toStdString();
                segments.push_back(std::move(seg));
            }
        }
        currentLines.clear();
    };

    const QStringList lines = scriptEdit_->toPlainText().split('\n');
    for (const QString& rawLine : lines) {
        const QString stripped = rawLine.trimmed();
        if (stripped.isEmpty()) {
            flush();
            currentSpeaker = -1;
            continue;
        }

        const QRegularExpressionMatch m = lineRe.match(stripped);
        if (m.hasMatch()) {
            flush();
            currentSpeaker = m.captured(1).toUpper().at(0).toLatin1() - 'A';
            const QString rest = m.captured(2).trimmed();
            if (!rest.isEmpty()) {
                currentLines << rest;
            }
        } else if (currentSpeaker >= 0) {
            currentLines << stripped;
        }
    }
    flush();

    return segments;
}

void MultiSpeakerPanel::resetToDefaults() {
    scriptEdit_->setPlainText("");
    speakerCountSpin_->setValue(2);
    currentSpeaker_ = 0;
    pauseSlider_->setValue(220);
    for (auto& settings : speakerSettings_) {
        settings = SpeakerSettings{};
    }
    onSpeakerCountChanged(speakerCountSpin_->value());
    speakerButtons_[0]->setChecked(true);
    loadSettingsToEditor(0);
    setPlaybackEnabled(false);
    setStatus("Write a dialogue script and click Render All.");
    setProgress(0);
}

QJsonObject MultiSpeakerPanel::toJson() const {
    QJsonObject obj;
    obj["script"] = scriptEdit_->toPlainText();
    obj["speakerCount"] = speakerCountSpin_->value();
    obj["currentSpeaker"] = currentSpeaker_;
    obj["pauseMs"] = pauseSlider_->value();

    QJsonArray speakers;
    for (int i = 0; i < kMaxSpeakers; ++i) {
        SpeakerSettings s = speakerSettings_[i];
        if (i == currentSpeaker_) {
            s.provider = speakerEditor_->provider();
            s.voiceAIndex = speakerEditor_->voiceAIndex();
            s.mixEnabled = speakerEditor_->mixEnabled();
            s.voiceBIndex = speakerEditor_->voiceBIndex();
            s.pctA = speakerEditor_->pctA();
            s.speed = speakerEditor_->speed();
            s.eqGainsDb = speakerEditor_->eqGainsDb();
            s.humanizerEnabled = speakerEditor_->humanizerEnabled();
        }
        QJsonObject speaker;
        speaker["provider"] = static_cast<int>(s.provider);
        speaker["voiceAIndex"] = s.voiceAIndex;
        speaker["mixEnabled"] = s.mixEnabled;
        speaker["voiceBIndex"] = s.voiceBIndex;
        speaker["pctA"] = s.pctA;
        speaker["speed"] = s.speed;
        speaker["humanizer"] = s.humanizerEnabled;
        QJsonArray eq;
        for (float g : s.eqGainsDb) {
            eq.append(g);
        }
        speaker["eqGainsDb"] = eq;
        speakers.append(speaker);
    }
    obj["speakerSettings"] = speakers;
    return obj;
}

void MultiSpeakerPanel::fromJson(const QJsonObject& obj) {
    scriptEdit_->setPlainText(obj["script"].toString());
    speakerCountSpin_->setValue(obj["speakerCount"].toInt(2));
    currentSpeaker_ = obj["currentSpeaker"].toInt(0);
    pauseSlider_->setValue(obj["pauseMs"].toInt(220));

    const QJsonArray speakers = obj["speakerSettings"].toArray();
    for (int i = 0; i < kMaxSpeakers && i < speakers.size(); ++i) {
        const QJsonObject speaker = speakers[i].toObject();
        SpeakerSettings& s = speakerSettings_[i];
        s.provider = static_cast<tts::Provider>(speaker["provider"].toInt(0));
        s.voiceAIndex = speaker["voiceAIndex"].toInt(0);
        s.mixEnabled = speaker["mixEnabled"].toBool(false);
        s.voiceBIndex = speaker["voiceBIndex"].toInt(1);
        s.pctA = speaker["pctA"].toInt(50);
        s.speed = static_cast<float>(speaker["speed"].toDouble(1.05));
        s.humanizerEnabled = speaker["humanizer"].toBool(false);
        const QJsonArray eq = speaker["eqGainsDb"].toArray();
        for (int b = 0; b < tts::GraphicEq::kNumBands && b < eq.size(); ++b) {
            s.eqGainsDb[b] = static_cast<float>(eq[b].toDouble());
        }
    }

    onSpeakerCountChanged(speakerCountSpin_->value());
    currentSpeaker_ = std::clamp(currentSpeaker_, 0, speakerCountSpin_->value() - 1);
    speakerButtons_[currentSpeaker_]->setChecked(true);
    loadSettingsToEditor(currentSpeaker_);
}

void MultiSpeakerPanel::onRenderClicked() {
    saveEditorToSettings(currentSpeaker_);

    MultiSynthRequest req;
    req.segments = parseScript();
    req.pauseMs = pauseSlider_->value();
    emit renderRequested(req);
}