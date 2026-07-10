#include "ModelManagerDialog.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

ModelManagerDialog::ModelManagerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Model Manager");
    resize(620, 480);

    auto* layout = new QVBoxLayout(this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* btnRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton("Refresh Status", this);
    syncBtn_ = new QPushButton("Sync Supertonic", this);
    fetchBtn_ = new QPushButton("Fetch ONNX Runtime", this);
    auto* kokoroBtn = new QPushButton("Setup Kokoro", this);
    auto* piperBtn = new QPushButton("Download Piper", this);
    auto* dfnBtn = new QPushButton("Setup DeepFilterNet", this);
    auto* openBtn = new QPushButton("Open Models Folder", this);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(syncBtn_);
    btnRow->addWidget(fetchBtn_);
    layout->addLayout(btnRow);

    auto* btnRow2 = new QHBoxLayout();
    btnRow2->addWidget(kokoroBtn);
    btnRow2->addWidget(piperBtn);
    btnRow2->addWidget(dfnBtn);
    btnRow2->addWidget(openBtn);
    btnRow2->addStretch(1);
    layout->addLayout(btnRow2);

    logEdit_ = new QPlainTextEdit(this);
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(500);
    layout->addWidget(logEdit_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(refreshBtn, &QPushButton::clicked, this, &ModelManagerDialog::onRefreshClicked);
    connect(syncBtn_, &QPushButton::clicked, this, &ModelManagerDialog::onSyncSupertonicClicked);
    connect(fetchBtn_, &QPushButton::clicked, this, &ModelManagerDialog::onFetchOnnxClicked);
    connect(kokoroBtn, &QPushButton::clicked, this, &ModelManagerDialog::onFetchKokoroClicked);
    connect(piperBtn, &QPushButton::clicked, this, &ModelManagerDialog::onFetchPiperClicked);
    connect(dfnBtn, &QPushButton::clicked, this, &ModelManagerDialog::onFetchDfnClicked);
    connect(openBtn, &QPushButton::clicked, this, &ModelManagerDialog::onOpenModelsFolderClicked);

    refreshStatus();
}

void ModelManagerDialog::appendLog(const QString& line) {
    logEdit_->appendPlainText(line);
}

void ModelManagerDialog::refreshStatus() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString supertonic = appDir + "/models/supertonic/onnx/vector_estimator.onnx";
    const QString kokoro = appDir + "/models/kokoro/kokoro-v1.0.onnx";
    const QString piper = appDir + "/models/piper";
    const QString dfn = appDir + "/models/deepfilternet/enc.onnx";
    const QString espeak = appDir + "/espeak-ng-data/phontab";

    QString html = "<b>Installed models:</b><ul>";
    html += QString("<li>Supertonic: %1</li>").arg(QFileInfo::exists(supertonic) ? "✅ found" : "❌ missing");
    html += QString("<li>Kokoro: %1</li>").arg(QFileInfo::exists(kokoro) ? "✅ found" : "⚠️ missing");
    html += QString("<li>Piper: %1</li>").arg(QDir(piper).exists() ? "✅ found" : "⚠️ missing");
    html += QString("<li>DeepFilterNet: %1</li>").arg(QFileInfo::exists(dfn) ? "✅ found" : "⚠️ missing");
    html += QString("<li>espeak-ng data: %1</li>").arg(QFileInfo::exists(espeak) ? "✅ found" : "❌ missing");
    html += "</ul>";
    statusLabel_->setText(html);
}

QString ModelManagerDialog::resolveScriptPath(const QString& scriptName) const {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/scripts/" + scriptName,
        QDir(appDir).absoluteFilePath("../../scripts/" + scriptName),
        QDir::current().absoluteFilePath("scripts/" + scriptName),
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

void ModelManagerDialog::runScript(const QString& scriptName) {
    const QString path = resolveScriptPath(scriptName);
    if (path.isEmpty()) {
        appendLog("ERROR: scripts/" + scriptName + " not found.");
        return;
    }
    if (process_) {
        process_->kill();
        process_->deleteLater();
    }
    process_ = new QProcess(this);
    connect(process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) { onProcessFinished(exitCode); });
    connect(process_, &QProcess::readyReadStandardOutput, this, [this]() {
        appendLog(QString::fromLocal8Bit(process_->readAllStandardOutput()).trimmed());
    });
    connect(process_, &QProcess::readyReadStandardError, this, [this]() {
        appendLog(QString::fromLocal8Bit(process_->readAllStandardError()).trimmed());
    });
    syncBtn_->setEnabled(false);
    fetchBtn_->setEnabled(false);
    appendLog("Running: " + path);
    process_->start("powershell", {"-ExecutionPolicy", "Bypass", "-File", path});
}

void ModelManagerDialog::onRefreshClicked() {
    refreshStatus();
    appendLog("Status refreshed.");
}

void ModelManagerDialog::onSyncSupertonicClicked() { runScript("sync_models.ps1"); }
void ModelManagerDialog::onFetchOnnxClicked() { runScript("fetch_onnxruntime.ps1"); }
void ModelManagerDialog::onFetchKokoroClicked() { runScript("fetch_kokoro.ps1"); }
void ModelManagerDialog::onFetchPiperClicked() { runScript("fetch_piper.ps1"); }
void ModelManagerDialog::onFetchDfnClicked() { runScript("fetch_deepfilternet.ps1"); }

void ModelManagerDialog::onOpenModelsFolderClicked() {
    const QString models = QCoreApplication::applicationDirPath() + "/models";
    QDir().mkpath(models);
    QDesktopServices::openUrl(QUrl::fromLocalFile(models));
}

void ModelManagerDialog::onProcessFinished(int exitCode) {
    syncBtn_->setEnabled(true);
    fetchBtn_->setEnabled(true);
    appendLog(QString("Process finished (exit %1).").arg(exitCode));
    refreshStatus();
}