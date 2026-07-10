#include "SpeakerVoiceCard.h"

#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "ComboUtils.h"
#include "EqPanel.h"
#include "ProjectManager.h"

namespace {

// Resolves the full-provider-list index for a filtered voice combo's selection
// (populateVoiceCombos() stores that index in each item's user data).
int comboVoiceFullIndex(const QComboBox* combo) {
    if (combo == nullptr || combo->currentIndex() < 0) {
        return -1;
    }
    const QVariant d = combo->currentData();
    return d.isValid() ? d.toInt() : combo->currentIndex();
}

} // namespace

SpeakerVoiceCard::SpeakerVoiceCard(const QString& title, QWidget* parent) : QGroupBox(title, parent) {
    auto* layout = new QVBoxLayout(this);

    auto* voiceRow = new QHBoxLayout();
    voiceRow->addWidget(new QLabel("Voice:", this));
    providerCombo_ = new QComboBox(this);
    providerCombo_->addItem("Supertonic (Multilingual)");
    providerCombo_->addItem("Kokoro (Local Neural)");
    providerCombo_->addItem("Piper (German)");
    providerCombo_->addItem("Microsoft Edge (Online)");
    providerCombo_->addItem("Fish Audio S2 (Kaggle)");
    voiceRow->addWidget(providerCombo_);

    // Gender + language filters (Edge only) — same style as Single Speaker.
    filterWidget_ = new QWidget(this);
    auto* filterRow = new QHBoxLayout(filterWidget_);
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(4);
    genderFilter_ = new QComboBox(filterWidget_);
    genderFilter_->addItems({"All genders", "Male", "Female"});
    genderFilter_->setToolTip("Filter Edge voices by gender");
    langFilter_ = new QComboBox(filterWidget_);
    langFilter_->addItem("All languages");
    for (const auto& loc : tts::edgeTtsLocales()) {
        langFilter_->addItem(QString::fromStdString(loc));
    }
    langFilter_->setToolTip("Filter Edge voices by language/locale");
    filterRow->addWidget(genderFilter_);
    filterRow->addWidget(langFilter_);
    voiceRow->addWidget(filterWidget_);

    voiceCombo_ = new QComboBox(this);
    voiceRow->addWidget(voiceCombo_, 1);
    layout->addLayout(voiceRow);

    // Preset + per-speaker Natural Humanizer row.
    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel("Preset:", this));
    presetCombo_ = new QComboBox(this);
    presetCombo_->setToolTip("Apply a saved voice preset to this speaker");
    presetRow->addWidget(presetCombo_, 1);
    humanizerCheck_ = new QCheckBox("Natural Humanizer", this);
    humanizerCheck_->setToolTip("Apply the Natural Humanizer DSP to this speaker (Edge voices)");
    presetRow->addWidget(humanizerCheck_);
    layout->addLayout(presetRow);

    auto reFilter = [this]() {
        const int keepFull = comboVoiceFullIndex(voiceCombo_);
        populateVoiceCombos();
        if (keepFull >= 0) {
            const int pos = voiceCombo_->findData(keepFull);
            if (pos >= 0) voiceCombo_->setCurrentIndex(pos);
        }
    };
    connect(genderFilter_, &QComboBox::currentIndexChanged, this, [reFilter](int) { reFilter(); });
    connect(langFilter_, &QComboBox::currentIndexChanged, this, [reFilter](int) { reFilter(); });
    connect(presetCombo_, &QComboBox::activated, this, &SpeakerVoiceCard::applyPreset);
    connect(humanizerCheck_, &QCheckBox::toggled, this, [this](bool) {
        emit editorSettingsChanged();
    });

    auto* mixRow = new QHBoxLayout();
    mixCheckBox_ = new QCheckBox("Mix with second voice", this);
    mixRow->addWidget(mixCheckBox_);
    voiceBCombo_ = new QComboBox(this);
    mixRow->addWidget(voiceBCombo_);
    mixSlider_ = new QSlider(Qt::Horizontal, this);
    mixSlider_->setRange(0, 100);
    mixSlider_->setValue(50);
    mixRow->addWidget(mixSlider_);
    mixLabel_ = new QLabel("A 50% / B 50%", this);
    mixRow->addWidget(mixLabel_);
    layout->addLayout(mixRow);

    auto* speedRow = new QHBoxLayout();
    speedRow->addWidget(new QLabel("Speed:", this));
    speedSlider_ = new QSlider(Qt::Horizontal, this);
    speedSlider_->setRange(70, 200); // 0.70x .. 2.00x
    speedSlider_->setValue(105);
    speedRow->addWidget(speedSlider_);
    speedLabel_ = new QLabel("1.05x", this);
    speedRow->addWidget(speedLabel_);
    eqDialogBtn_ = new QPushButton("EQ...", this);
    eqDialogBtn_->setCheckable(true);
    eqDialogBtn_->setFixedWidth(52);
    speedRow->addWidget(eqDialogBtn_);
    layout->addLayout(speedRow);

    layout->addStretch(1); // keeps rows compact; absorbs leftover space at bottom of card

    // EQ lives in a persistent non-modal dialog opened by the "EQ..." button.
    eqDialog_ = new QDialog(this);
    eqDialog_->setWindowTitle(title + " — Equalizer");
    eqDialog_->setWindowFlag(Qt::Tool); // floats above without stealing focus
    auto* eqDlgLayout = new QVBoxLayout(eqDialog_);
    eqDlgLayout->setContentsMargins(4, 4, 4, 4);
    eqPanel_ = new EqPanel(eqDialog_);
    eqDlgLayout->addWidget(eqPanel_);
    eqDialog_->resize(660, 200);

    connect(providerCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        populateVoiceCombos();
        updateVoiceFilterUi();
        updateMixUiEnabled();
        emit providerIndexChanged(index);
    });
    connect(mixCheckBox_, &QCheckBox::toggled, this, &SpeakerVoiceCard::updateMixUiEnabled);
    connect(mixSlider_, &QSlider::valueChanged, this, [this](int v) {
        mixLabel_->setText(QString("A %1% / B %2%").arg(v).arg(100 - v));
    });
    connect(speedSlider_, &QSlider::valueChanged, this, [this](int v) {
        speedLabel_->setText(QString::number(v / 100.0, 'f', 2) + "x");
        emit editorSettingsChanged();
    });
    connect(eqDialogBtn_, &QPushButton::clicked, this, [this](bool) {
        if (eqDialog_->isVisible()) {
            eqDialog_->hide();
        } else {
            eqDialog_->show();
            eqDialog_->raise();
        }
        eqDialogBtn_->setChecked(eqDialog_->isVisible());
    });
    connect(eqDialog_, &QDialog::finished, this, [this](int) {
        eqDialogBtn_->setChecked(false);
    });

    // Neat, fixed-height scrollable dropdowns (same as the Single Speaker voice
    // picker). Voice lists are also type-to-search.
    tts::applyNeatComboPopup(providerCombo_, /*searchable=*/false);
    tts::applyNeatComboPopup(genderFilter_, /*searchable=*/false);
    tts::applyNeatComboPopup(langFilter_, /*searchable=*/true);  // long locale list
    tts::applyNeatComboPopup(voiceCombo_, /*searchable=*/true);
    tts::applyNeatComboPopup(voiceBCombo_, /*searchable=*/true);
    tts::applyNeatComboPopup(presetCombo_, /*searchable=*/false);

    refreshPresetCombo();
    populateVoiceCombos();
    updateVoiceFilterUi();
    updateMixUiEnabled();
}

void SpeakerVoiceCard::populateVoiceCombos() {
    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const auto& voices = tts::voicesForProvider(provider);

    const bool isEdge = (provider == tts::Provider::EdgeTts);
    const QString genderSel = (isEdge && genderFilter_) ? genderFilter_->currentText() : QString();
    const QString langSel = (isEdge && langFilter_ && langFilter_->currentIndex() > 0)
                                ? langFilter_->currentText() : QString();
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
        voiceCombo_->addItem(label, i);  // store full-list index in user data
        voiceBCombo_->addItem(label, i);
    }
    voiceCombo_->blockSignals(false);
    voiceBCombo_->blockSignals(false);
    if (voiceBCombo_->count() > 1) {
        voiceBCombo_->setCurrentIndex(1);
    }
}

void SpeakerVoiceCard::updateMixUiEnabled() {
    const auto provider = static_cast<tts::Provider>(providerCombo_->currentIndex());
    const bool supportsMix = tts::supportsVoiceMixing(provider);
    mixCheckBox_->setEnabled(supportsMix);
    if (!supportsMix) {
        mixCheckBox_->setChecked(false);
    }
    const bool mixOn = supportsMix && mixCheckBox_->isChecked();
    voiceBCombo_->setEnabled(mixOn);
    mixSlider_->setEnabled(mixOn);
    mixLabel_->setEnabled(mixOn);
}

tts::Provider SpeakerVoiceCard::provider() const {
    return static_cast<tts::Provider>(providerCombo_->currentIndex());
}

tts::VoiceEntry SpeakerVoiceCard::voiceA() const {
    const auto& voices = tts::voicesForProvider(provider());
    const int idx = comboVoiceFullIndex(voiceCombo_);
    if (idx >= 0 && idx < static_cast<int>(voices.size())) {
        return voices[idx];
    }
    return {};
}

bool SpeakerVoiceCard::mixEnabled() const {
    return tts::supportsVoiceMixing(provider()) && mixCheckBox_->isChecked();
}

tts::VoiceEntry SpeakerVoiceCard::voiceB() const {
    const auto& voices = tts::voicesForProvider(provider());
    const int idx = comboVoiceFullIndex(voiceBCombo_);
    if (idx >= 0 && idx < static_cast<int>(voices.size())) {
        return voices[idx];
    }
    return {};
}

int SpeakerVoiceCard::pctA() const {
    return mixSlider_->value();
}

float SpeakerVoiceCard::speed() const {
    return speedSlider_->value() / 100.0f;
}

std::array<float, tts::GraphicEq::kNumBands> SpeakerVoiceCard::eqGainsDb() const {
    return eqPanel_->getGainsDb();
}

int SpeakerVoiceCard::voiceAIndex() const {
    return comboVoiceFullIndex(voiceCombo_);
}

int SpeakerVoiceCard::voiceBIndex() const {
    return comboVoiceFullIndex(voiceBCombo_);
}

void SpeakerVoiceCard::setProvider(tts::Provider provider) {
    providerCombo_->setCurrentIndex(static_cast<int>(provider));
}

void SpeakerVoiceCard::setVoiceAIndex(int index) {
    // `index` is a full-provider-list index. If a filter hides it, clear the
    // filters and repopulate so the saved voice can be re-selected.
    int pos = voiceCombo_->findData(index);
    if (pos < 0 && index >= 0) {
        if (genderFilter_) genderFilter_->setCurrentIndex(0);
        if (langFilter_) langFilter_->setCurrentIndex(0);
        populateVoiceCombos();
        pos = voiceCombo_->findData(index);
    }
    if (pos >= 0) voiceCombo_->setCurrentIndex(pos);
}

void SpeakerVoiceCard::setMixEnabled(bool enabled) {
    mixCheckBox_->setChecked(enabled && mixCheckBox_->isEnabled());
}

void SpeakerVoiceCard::setVoiceBIndex(int index) {
    const int pos = voiceBCombo_->findData(index);
    if (pos >= 0) voiceBCombo_->setCurrentIndex(pos);
}

void SpeakerVoiceCard::setPctA(int pct) {
    mixSlider_->setValue(pct);
}

void SpeakerVoiceCard::setSpeed(float speed) {
    speedSlider_->setValue(static_cast<int>(std::lround(speed * 100.0f)));
}

void SpeakerVoiceCard::setEqGainsDb(const std::array<float, tts::GraphicEq::kNumBands>& gains) {
    eqPanel_->setGainsDb(gains);
}

bool SpeakerVoiceCard::humanizerEnabled() const {
    return humanizerCheck_ != nullptr && humanizerCheck_->isChecked();
}

void SpeakerVoiceCard::setHumanizerEnabled(bool enabled) {
    if (humanizerCheck_) humanizerCheck_->setChecked(enabled);
}

void SpeakerVoiceCard::updateVoiceFilterUi() {
    const bool isEdge = (static_cast<tts::Provider>(providerCombo_->currentIndex()) ==
                         tts::Provider::EdgeTts);
    if (filterWidget_) filterWidget_->setVisible(isEdge);
    if (humanizerCheck_) {
        humanizerCheck_->setEnabled(isEdge);
        if (!isEdge) humanizerCheck_->setChecked(false);
    }
}

void SpeakerVoiceCard::refreshPresetCombo() {
    if (presetCombo_ == nullptr) return;
    presetCombo_->blockSignals(true);
    presetCombo_->clear();
    presetCombo_->addItem("(Select a preset)");
    for (const VoicePreset& p : ProjectManager::loadVoicePresets()) {
        presetCombo_->addItem(p.name);
    }
    presetCombo_->setCurrentIndex(0);
    presetCombo_->blockSignals(false);
}

void SpeakerVoiceCard::applyPreset(int comboIndex) {
    if (comboIndex <= 0) return;
    const std::vector<VoicePreset> presets = ProjectManager::loadVoicePresets();
    const int presetIdx = comboIndex - 1;
    if (presetIdx < 0 || presetIdx >= static_cast<int>(presets.size())) return;

    const VoicePreset& p = presets[static_cast<size_t>(presetIdx)];
    setProvider(p.provider);
    populateVoiceCombos();
    updateVoiceFilterUi();
    updateMixUiEnabled();

    // Resolve the preset's voices (stored by shortName) to full-list indices.
    const auto& voices = tts::voicesForProvider(p.provider);
    auto findShort = [&voices](const QString& shortName) -> int {
        for (int i = 0; i < static_cast<int>(voices.size()); ++i) {
            if (QString::fromStdString(voices[static_cast<size_t>(i)].shortName) == shortName) {
                return i;
            }
        }
        return -1;
    };
    setVoiceAIndex(findShort(p.voiceAShort));
    setMixEnabled(p.mixEnabled);
    setVoiceBIndex(findShort(p.voiceBShort));
    setPctA(p.pctA);
    setSpeed(p.speed);
    setEqGainsDb(p.eqGainsDb);
    emit editorSettingsChanged();
}
