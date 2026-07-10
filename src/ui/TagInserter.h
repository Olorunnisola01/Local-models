#pragma once

#include <QWidget>

class QComboBox;
class QPlainTextEdit;
class QPushButton;

// Dropdown + Insert button for common inline TTS tags.
class TagInserter : public QWidget {
    Q_OBJECT
public:
    explicit TagInserter(QPlainTextEdit* target, QWidget* parent = nullptr);

    void setProviderIndex(int providerIndex);

signals:
    void tagInserted(const QString& tag);

private slots:
    void onInsertClicked();

private:
    void rebuildTags();

    QPlainTextEdit* target_ = nullptr;
    QComboBox* tagCombo_ = nullptr;
    QPushButton* insertBtn_ = nullptr;
    int providerIndex_ = 0;
};