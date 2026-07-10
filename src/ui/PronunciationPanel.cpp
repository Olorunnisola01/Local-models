#include "PronunciationPanel.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

PronunciationPanel::PronunciationPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    helpLabel_ = new QLabel(
        "Markup: <tt>[pause=300ms]</tt> inline pause &nbsp;|&nbsp; "
        "<tt>[emph]word[/emph]</tt> or <tt>*word*</tt> emphasis (Edge TTS) &nbsp;|&nbsp; "
        "Dictionary replaces whole words before synthesis.",
        this);
    helpLabel_->setWordWrap(true);
    layout->addWidget(helpLabel_);

    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({"Word", "Pronunciation / replacement"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_);

    auto* dictBtnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton("Add Entry", this);
    auto* removeBtn = new QPushButton("Remove Selected", this);
    dictBtnRow->addWidget(addBtn);
    dictBtnRow->addWidget(removeBtn);
    dictBtnRow->addStretch(1);
    layout->addLayout(dictBtnRow);

    auto* previewRow = new QHBoxLayout();
    previewRow->addWidget(new QLabel("Preview source text from Single Speaker tab:", this));
    auto* previewBtn = new QPushButton("Preview Processed Text", this);
    previewRow->addStretch(1);
    previewRow->addWidget(previewBtn);
    layout->addLayout(previewRow);

    previewEdit_ = new QPlainTextEdit(this);
    previewEdit_->setReadOnly(true);
    previewEdit_->setPlaceholderText("Click Preview to see dictionary + markup applied to the current text.");
    previewEdit_->setMinimumHeight(100);
    layout->addWidget(previewEdit_);

    connect(addBtn, &QPushButton::clicked, this, &PronunciationPanel::onAddEntry);
    connect(removeBtn, &QPushButton::clicked, this, &PronunciationPanel::onRemoveEntry);
    connect(previewBtn, &QPushButton::clicked, this, &PronunciationPanel::onPreviewClicked);
    connect(table_, &QTableWidget::itemChanged, this, [this]() { emit dictionaryChanged(); });
}

std::vector<tts::PronunciationEntry> PronunciationPanel::dictionary() const {
    std::vector<tts::PronunciationEntry> entries;
    for (int row = 0; row < table_->rowCount(); ++row) {
        tts::PronunciationEntry entry;
        if (table_->item(row, 0)) {
            entry.word = table_->item(row, 0)->text().trimmed().toStdString();
        }
        if (table_->item(row, 1)) {
            entry.pronunciation = table_->item(row, 1)->text().trimmed().toStdString();
        }
        if (!entry.word.empty() && !entry.pronunciation.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

void PronunciationPanel::setDictionary(const std::vector<tts::PronunciationEntry>& entries) {
    rebuildTableFromEntries(entries);
}

QJsonArray PronunciationPanel::dictionaryToJson() const {
    QJsonArray arr;
    for (const tts::PronunciationEntry& entry : dictionary()) {
        QJsonObject obj;
        obj["word"] = QString::fromStdString(entry.word);
        obj["pronunciation"] = QString::fromStdString(entry.pronunciation);
        arr.append(obj);
    }
    return arr;
}

void PronunciationPanel::dictionaryFromJson(const QJsonArray& arr) {
    std::vector<tts::PronunciationEntry> entries;
    for (const QJsonValue& val : arr) {
        const QJsonObject obj = val.toObject();
        tts::PronunciationEntry entry;
        entry.word = obj["word"].toString().trimmed().toStdString();
        entry.pronunciation = obj["pronunciation"].toString().trimmed().toStdString();
        if (!entry.word.empty() && !entry.pronunciation.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    setDictionary(entries);
}

QString PronunciationPanel::previewProcessedText(const QString& sourceText) const {
    const std::vector<tts::SpeechPart> parts =
        tts::prepareSpeechParts(sourceText.toStdString(), dictionary());

    QString out;
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string speakable = tts::stripMarkup(parts[i].text);
        if (!speakable.empty()) {
            if (!out.isEmpty()) {
                out += "\n";
            }
            out += QString::fromStdString(speakable);
        }
        if (parts[i].pauseAfterMs > 0) {
            out += QString("\n[pause %1 ms after this part]").arg(parts[i].pauseAfterMs);
        }
    }
    return out;
}

void PronunciationPanel::rebuildTableFromEntries(const std::vector<tts::PronunciationEntry>& entries) {
    table_->blockSignals(true);
    table_->setRowCount(0);
    for (const tts::PronunciationEntry& entry : entries) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(entry.word)));
        table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(entry.pronunciation)));
    }
    table_->blockSignals(false);
}

void PronunciationPanel::onAddEntry() {
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(""));
    table_->setItem(row, 1, new QTableWidgetItem(""));
    table_->setCurrentCell(row, 0);
    emit dictionaryChanged();
}

void PronunciationPanel::onRemoveEntry() {
    const int row = table_->currentRow();
    if (row >= 0) {
        table_->removeRow(row);
        emit dictionaryChanged();
    }
}

void PronunciationPanel::showPreviewFor(const QString& sourceText) {
    previewEdit_->setPlainText(previewProcessedText(sourceText));
}

void PronunciationPanel::onPreviewClicked() {
    emit previewRequested();
}