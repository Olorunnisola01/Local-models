#include "BatchQueueDialog.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

BatchQueueDialog::BatchQueueDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Batch / Chapter Queue");
    resize(620, 520);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        "Add .txt files or a folder of chapters. Each file is synthesized using the current "
        "provider/voice settings and exported in the chosen format.",
        this));

    auto* btnRow = new QHBoxLayout();
    auto* addFilesBtn = new QPushButton("Add Files...", this);
    auto* addFolderBtn = new QPushButton("Add Folder...", this);
    auto* outDirBtn = new QPushButton("Output Folder...", this);
    btnRow->addWidget(addFilesBtn);
    btnRow->addWidget(addFolderBtn);
    btnRow->addWidget(outDirBtn);
    btnRow->addStretch(1);
    layout->addLayout(btnRow);

    auto* formatRow = new QHBoxLayout();
    formatRow->addWidget(new QLabel("Export format:", this));
    formatCombo_ = new QComboBox(this);
    formatCombo_->addItems({"WAV", "MP3", "FLAC"});
    formatRow->addWidget(formatCombo_);
    formatRow->addStretch(1);
    auto* saveProfileBtn = new QPushButton("Save Profile", this);
    auto* loadProfileBtn = new QPushButton("Load Profile", this);
    formatRow->addWidget(saveProfileBtn);
    formatRow->addWidget(loadProfileBtn);
    layout->addLayout(formatRow);

    outputDirLabel_ = new QLabel("Output: (not set)", this);
    layout->addWidget(outputDirLabel_);

    queueEdit_ = new QPlainTextEdit(this);
    queueEdit_->setReadOnly(true);
    queueEdit_->setPlaceholderText("Queued files will appear here...");
    layout->addWidget(queueEdit_, 1);

    progressBar_ = new QProgressBar(this);
    layout->addWidget(progressBar_);

    statusLabel_ = new QLabel("Idle", this);
    layout->addWidget(statusLabel_);

    auto* actionRow = new QHBoxLayout();
    startBtn_ = new QPushButton("Start Queue", this);
    cancelBtn_ = new QPushButton("Cancel", this);
    cancelBtn_->setEnabled(false);
    actionRow->addWidget(startBtn_);
    actionRow->addWidget(cancelBtn_);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

    connect(addFilesBtn, &QPushButton::clicked, this, &BatchQueueDialog::onAddFilesClicked);
    connect(addFolderBtn, &QPushButton::clicked, this, &BatchQueueDialog::onAddFolderClicked);
    connect(outDirBtn, &QPushButton::clicked, this, &BatchQueueDialog::onChooseOutputDirClicked);
    connect(startBtn_, &QPushButton::clicked, this, &BatchQueueDialog::onStartClicked);
    connect(cancelBtn_, &QPushButton::clicked, this, &BatchQueueDialog::onCancelClicked);
    connect(saveProfileBtn, &QPushButton::clicked, this, &BatchQueueDialog::onSaveProfileClicked);
    connect(loadProfileBtn, &QPushButton::clicked, this, &BatchQueueDialog::onLoadProfileClicked);

    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    outputDir_ = settings.value("batchOutputDir").toString();
    if (!outputDir_.isEmpty()) {
        outputDirLabel_->setText("Output: " + outputDir_);
    }
    formatCombo_->setCurrentText(settings.value("batchFormat", "WAV").toString());
}

QStringList BatchQueueDialog::inputPaths() const { return paths_; }
QString BatchQueueDialog::outputDirectory() const { return outputDir_; }
QString BatchQueueDialog::outputFormat() const { return formatCombo_->currentText(); }

QString BatchQueueDialog::outputExtension() const {
    const QString fmt = formatCombo_->currentText().toLower();
    if (fmt == "mp3") {
        return ".mp3";
    }
    if (fmt == "flac") {
        return ".flac";
    }
    return ".wav";
}

void BatchQueueDialog::refreshList() {
    QStringList lines;
    for (const QString& p : paths_) {
        lines << p;
    }
    queueEdit_->setPlainText(lines.join("\n"));
}

void BatchQueueDialog::onAddFilesClicked() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Add text files", QString(),
                                                            "Text Files (*.txt);;All Files (*)");
    for (const QString& f : files) {
        if (!paths_.contains(f)) {
            paths_.push_back(f);
        }
    }
    refreshList();
}

void BatchQueueDialog::onAddFolderClicked() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Add folder of chapters");
    if (dir.isEmpty()) {
        return;
    }
    QDir folder(dir);
    const QStringList txts = folder.entryList({"*.txt"}, QDir::Files, QDir::Name);
    for (const QString& name : txts) {
        const QString path = folder.absoluteFilePath(name);
        if (!paths_.contains(path)) {
            paths_.push_back(path);
        }
    }
    refreshList();
}

void BatchQueueDialog::onChooseOutputDirClicked() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Choose output folder");
    if (!dir.isEmpty()) {
        outputDir_ = dir;
        outputDirLabel_->setText("Output: " + outputDir_);
        QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
        settings.setValue("batchOutputDir", outputDir_);
    }
}

void BatchQueueDialog::onSaveProfileClicked() {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue("batchOutputDir", outputDir_);
    settings.setValue("batchFormat", formatCombo_->currentText());
    statusLabel_->setText("Batch profile saved.");
}

void BatchQueueDialog::onLoadProfileClicked() {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    outputDir_ = settings.value("batchOutputDir").toString();
    if (!outputDir_.isEmpty()) {
        outputDirLabel_->setText("Output: " + outputDir_);
    }
    formatCombo_->setCurrentText(settings.value("batchFormat", "WAV").toString());
    statusLabel_->setText("Batch profile loaded.");
}

void BatchQueueDialog::onStartClicked() {
    if (paths_.isEmpty()) {
        statusLabel_->setText("Add at least one file.");
        return;
    }
    if (outputDir_.isEmpty()) {
        statusLabel_->setText("Choose an output folder first.");
        return;
    }
    running_ = true;
    cancelRequested_ = false;
    currentIndex_ = -1;
    startBtn_->setEnabled(false);
    cancelBtn_->setEnabled(true);
    progressBar_->setRange(0, static_cast<int>(paths_.size()));
    progressBar_->setValue(0);
    processNext();
}

void BatchQueueDialog::onCancelClicked() {
    cancelRequested_ = true;
    statusLabel_->setText("Cancelling...");
    emit cancelRequested();
}

void BatchQueueDialog::processNext() {
    if (cancelRequested_) {
        running_ = false;
        startBtn_->setEnabled(true);
        cancelBtn_->setEnabled(false);
        statusLabel_->setText("Cancelled.");
        emit batchFinished();
        return;
    }
    ++currentIndex_;
    if (currentIndex_ >= paths_.size()) {
        running_ = false;
        startBtn_->setEnabled(true);
        cancelBtn_->setEnabled(false);
        statusLabel_->setText("Batch complete.");
        emit batchFinished();
        return;
    }
    const QString path = paths_[currentIndex_];
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        onItemFinished(currentIndex_, false, "Could not read file.");
        return;
    }
    const QString text = QString::fromUtf8(f.readAll());
    const QString base = QFileInfo(path).completeBaseName();
    const QString outPath = outputDir_ + "/" + base + outputExtension();
    statusLabel_->setText(QString("Processing %1 (%2/%3)...")
                               .arg(QFileInfo(path).fileName())
                               .arg(currentIndex_ + 1)
                               .arg(paths_.size()));
    emit processItemRequested(text, outPath, currentIndex_);
}

void BatchQueueDialog::onItemFinished(int itemIndex, bool success, const QString& errorMessage) {
    if (itemIndex != currentIndex_) {
        return;
    }
    progressBar_->setValue(currentIndex_ + 1);
    if (!success) {
        const QString reason = errorMessage.isEmpty() ? "Unknown error" : errorMessage;
        statusLabel_->setText(QString("Failed: %1 — %2").arg(paths_[itemIndex], reason));
    }
    processNext();
}

void BatchQueueDialog::setBusy(bool busy) {
    startBtn_->setEnabled(!busy && !running_);
}