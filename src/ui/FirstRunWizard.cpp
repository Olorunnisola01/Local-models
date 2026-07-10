#include "FirstRunWizard.h"
#include "ReadMeDialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "../core/GpuDevice.h"

FirstRunWizard::FirstRunWizard(const QString& modelDir, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Welcome to EdgeTTS-Studio Native");
    resize(560, 480);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        "<h3>First-run setup</h3>"
        "<p>Let's verify your environment before you start synthesizing speech.</p>",
        this));

    checksLabel_ = new QLabel(this);
    checksLabel_->setWordWrap(true);
    checksLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(checksLabel_);

    const QString appDir = QCoreApplication::applicationDirPath();
    QString html = "<ul>";
    const bool supertonicOk = QFileInfo::exists(modelDir + "/vector_estimator.onnx");
    html += QString("<li>%1 <b>Supertonic models</b> — %2</li>")
                .arg(supertonicOk ? "✅" : "⚠️")
                .arg(supertonicOk ? "found" : "Use Tools → Model Manager");

    const bool kokoroOk = QFileInfo::exists(appDir + "/models/kokoro/kokoro-v1.0.onnx");
    html += QString("<li>%1 <b>Kokoro models</b> — %2</li>")
                .arg(kokoroOk ? "✅" : "⚠️")
                .arg(kokoroOk ? "found" : "optional — Model Manager → Setup Kokoro");

    const bool piperOk = QDir(appDir + "/models/piper").exists();
    html += QString("<li>%1 <b>Piper models</b> — %2</li>")
                .arg(piperOk ? "✅" : "⚠️")
                .arg(piperOk ? "found" : "optional — Model Manager → Download Piper");

    const bool dfnOk = QFileInfo::exists(appDir + "/models/deepfilternet/enc.onnx");
    html += QString("<li>%1 <b>DeepFilterNet</b> — %2</li>")
                .arg(dfnOk ? "✅" : "⚠️")
                .arg(dfnOk ? "found" : "optional — for Enhance (denoise)");

    const bool espeakOk = QFileInfo::exists(appDir + "/espeak-ng-data/phontab");
    html += QString("<li>%1 <b>espeak-ng data</b> — %2</li>")
                .arg(espeakOk ? "✅" : "❌")
                .arg(espeakOk ? "found" : "required for Kokoro/Piper — rebuild app");

    const tts::GpuDeviceInfo gpu = tts::selectBestDmlDevice();
    const bool gpuOk = gpu.deviceId >= 0;
    html += QString("<li>%1 <b>GPU acceleration (DirectML/CUDA)</b> — %2</li>")
                .arg(gpuOk ? "✅" : "⚠️")
                .arg(gpuOk ? QString::fromStdString(gpu.name) : "CPU fallback will be used");

    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    const bool remoteConfigured = !settings.value("remoteKokoroUrl").toString().isEmpty() ||
                                  !settings.value("remoteFishUrl").toString().isEmpty();
    html += QString("<li>%1 <b>Remote Kaggle URL</b> — %2</li>")
                .arg(remoteConfigured ? "✅" : "⚠️")
                .arg(remoteConfigured ? "configured" : "optional — Remote GPU panel");
    html += "</ul>";
    checksLabel_->setText(html);

    auto* readmeBtn = new QPushButton("Open Text Tags Guide (Read me)...", this);
    connect(readmeBtn, &QPushButton::clicked, this, [this]() {
        ReadMeDialog dlg(this);
        dlg.exec();
    });
    layout->addWidget(readmeBtn);

    auto* dontShow = new QCheckBox("Don't show this again on startup", this);
    dontShow->setChecked(true);
    layout->addWidget(dontShow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, dontShow]() {
        if (dontShow->isChecked()) {
            markComplete();
        }
        accept();
    });
    layout->addWidget(buttons);
}

bool FirstRunWizard::shouldShow() {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    return !settings.value("firstRunComplete", false).toBool();
}

void FirstRunWizard::markComplete() {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue("firstRunComplete", true);
}