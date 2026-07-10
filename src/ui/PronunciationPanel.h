#pragma once

#include <vector>

#include <QJsonArray>
#include <QWidget>

#include "../core/TextMarkup.h"

class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;

// Pronunciation studio: custom dictionary + inline markup preview.
class PronunciationPanel : public QWidget {
    Q_OBJECT
public:
    explicit PronunciationPanel(QWidget* parent = nullptr);

    std::vector<tts::PronunciationEntry> dictionary() const;
    void setDictionary(const std::vector<tts::PronunciationEntry>& entries);

    QJsonArray dictionaryToJson() const;
    void dictionaryFromJson(const QJsonArray& arr);

    QString previewProcessedText(const QString& sourceText) const;
    void showPreviewFor(const QString& sourceText);

signals:
    void dictionaryChanged();
    void previewRequested();

private slots:
    void onAddEntry();
    void onRemoveEntry();
    void onPreviewClicked();

private:
    void rebuildTableFromEntries(const std::vector<tts::PronunciationEntry>& entries);

    QTableWidget* table_ = nullptr;
    QPlainTextEdit* previewEdit_ = nullptr;
    QLabel* helpLabel_ = nullptr;
};