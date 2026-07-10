#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QPlainTextEdit;
class QProgressBar;
class QLabel;
class QPushButton;

// Batch synthesis: queue multiple text files or chapters for overnight rendering.
class BatchQueueDialog : public QDialog {
    Q_OBJECT
public:
    struct BatchItem {
        QString inputPath;
        QString outputPath;
        bool done = false;
        bool failed = false;
    };

    explicit BatchQueueDialog(QWidget* parent = nullptr);

    QStringList inputPaths() const;
    QString outputDirectory() const;
    QString outputFormat() const;
    bool isCancelRequested() const { return cancelRequested_; }

signals:
    void processItemRequested(const QString& text, const QString& outputPath, int itemIndex);
    void batchFinished();
    void cancelRequested();

public slots:
    void onItemFinished(int itemIndex, bool success, const QString& errorMessage = {});
    void setBusy(bool busy);

private slots:
    void onAddFilesClicked();
    void onAddFolderClicked();
    void onChooseOutputDirClicked();
    void onStartClicked();
    void onCancelClicked();
    void onSaveProfileClicked();
    void onLoadProfileClicked();

private:
    void refreshList();
    void processNext();
    QString outputExtension() const;

    QPlainTextEdit* queueEdit_ = nullptr;
    QLabel* outputDirLabel_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;

    QString outputDir_;
    QStringList paths_;
    int currentIndex_ = -1;
    bool running_ = false;
    bool cancelRequested_ = false;
};