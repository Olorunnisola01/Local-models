#include "RemoteGpuPanel.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

RemoteGpuPanel::RemoteGpuPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* group = new QGroupBox("Remote GPU (Kaggle)", this);
    auto* groupLayout = new QVBoxLayout(group);

    kokoroHint_ = new QLabel(
        "When enabled, Kokoro synthesis routes to the remote server instead of local models.",
        group);
    kokoroHint_->setWordWrap(true);
    kokoroHint_->setStyleSheet("color: palette(mid); font-size: 11px;");
    groupLayout->addWidget(kokoroHint_);

    auto* kokoroRow = new QHBoxLayout();
    kokoroCheck_ = new QCheckBox("Route Kokoro to remote server:", group);
    kokoroUrlEdit_ = new QLineEdit(group);
    kokoroUrlEdit_->setPlaceholderText("https://xxxx.trycloudflare.com");
    kokoroStatus_ = new QLabel("● offline", group);
    kokoroStatus_->setMinimumWidth(80);
    stopKokoroBtn_ = new QPushButton("Stop", group);
    stopKokoroBtn_->setToolTip("Shut down the Kokoro Kaggle session");
    kokoroRow->addWidget(kokoroCheck_);
    kokoroRow->addWidget(kokoroUrlEdit_, 1);
    kokoroRow->addWidget(kokoroStatus_);
    kokoroRow->addWidget(stopKokoroBtn_);
    groupLayout->addLayout(kokoroRow);

    fishSection_ = new QWidget(group);
    auto* fishRow = new QHBoxLayout(fishSection_);
    fishRow->setContentsMargins(0, 0, 0, 0);
    fishCheck_ = new QCheckBox("Fish Audio S2 server:", fishSection_);
    fishUrlEdit_ = new QLineEdit(fishSection_);
    fishUrlEdit_->setPlaceholderText("https://xxxx.trycloudflare.com");
    fishStatus_ = new QLabel("● offline", fishSection_);
    fishStatus_->setMinimumWidth(80);
    stopFishBtn_ = new QPushButton("Stop", fishSection_);
    stopFishBtn_->setToolTip("Shut down the Fish Audio Kaggle session");
    fishRow->addWidget(fishCheck_);
    fishRow->addWidget(fishUrlEdit_, 1);
    fishRow->addWidget(fishStatus_);
    fishRow->addWidget(stopFishBtn_);
    groupLayout->addWidget(fishSection_);

    auto* edgeRow = new QHBoxLayout();
    testEdgeBtn_ = new QPushButton("Test Edge TTS Connection", group);
    edgeStatus_ = new QLabel("Edge TTS: not tested", group);
    edgeRow->addWidget(testEdgeBtn_);
    edgeRow->addWidget(edgeStatus_, 1);
    groupLayout->addLayout(edgeRow);

    layout->addWidget(group);

    connect(kokoroCheck_, &QCheckBox::toggled, this, &RemoteGpuPanel::settingsChanged);
    connect(kokoroCheck_, &QCheckBox::toggled, this, &RemoteGpuPanel::kokoroToggled);
    connect(fishCheck_, &QCheckBox::toggled, this, &RemoteGpuPanel::settingsChanged);
    connect(kokoroUrlEdit_, &QLineEdit::editingFinished, this, &RemoteGpuPanel::settingsChanged);
    connect(fishUrlEdit_, &QLineEdit::editingFinished, this, &RemoteGpuPanel::settingsChanged);
    connect(stopKokoroBtn_, &QPushButton::clicked, this, &RemoteGpuPanel::onStopKokoroClicked);
    connect(stopFishBtn_, &QPushButton::clicked, this, &RemoteGpuPanel::onStopFishClicked);
    connect(testEdgeBtn_, &QPushButton::clicked, this, &RemoteGpuPanel::onTestEdgeTtsClicked);

    pingTimer_ = new QTimer(this);
    connect(pingTimer_, &QTimer::timeout, this, &RemoteGpuPanel::onPingTick);
    pingTimer_->start(15000);
    onPingTick();
}

bool RemoteGpuPanel::kokoroEnabled() const { return kokoroCheck_->isChecked(); }
QString RemoteGpuPanel::kokoroUrl() const { return kokoroUrlEdit_->text().trimmed(); }
bool RemoteGpuPanel::fishEnabled() const { return fishCheck_->isChecked(); }
QString RemoteGpuPanel::fishUrl() const { return fishUrlEdit_->text().trimmed(); }

void RemoteGpuPanel::setEdgeTestInProgress(bool inProgress) {
    testEdgeBtn_->setEnabled(!inProgress);
    if (inProgress) {
        edgeStatus_->setText("Edge TTS: testing...");
        edgeStatus_->setStyleSheet("color: palette(text); font-weight: normal;");
    }
}

void RemoteGpuPanel::setEdgeTestResult(bool ok, const QString& message) {
    testEdgeBtn_->setEnabled(true);
    if (ok) {
        edgeStatus_->setText("Edge TTS: " + message);
        edgeStatus_->setStyleSheet("color: #50c878; font-weight: bold;");
    } else {
        edgeStatus_->setText("Edge TTS: failed — " + message);
        edgeStatus_->setStyleSheet("color: #dc5050; font-weight: bold;");
    }
    emit statusChanged();
}

void RemoteGpuPanel::loadFromSettings() {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    kokoroCheck_->setChecked(settings.value("useRemoteKokoro", false).toBool());
    kokoroUrlEdit_->setText(settings.value("remoteKokoroUrl").toString());
    fishCheck_->setChecked(settings.value("useRemoteFish", false).toBool());
    fishUrlEdit_->setText(settings.value("remoteFishUrl").toString());
}

void RemoteGpuPanel::saveToSettings() const {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue("useRemoteKokoro", kokoroCheck_->isChecked());
    settings.setValue("remoteKokoroUrl", kokoroUrlEdit_->text().trimmed());
    settings.setValue("useRemoteFish", fishCheck_->isChecked());
    settings.setValue("remoteFishUrl", fishUrlEdit_->text().trimmed());
}

void RemoteGpuPanel::setStatus(QLabel* label, const QString& text, const QColor& color) {
    label->setText(text);
    label->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color.name()));
    emit statusChanged();
}

QString RemoteGpuPanel::statusSummary() const {
    return QString("Kokoro %1  |  Fish %2").arg(kokoroStatus_->text(), fishStatus_->text());
}

void RemoteGpuPanel::pingUrl(const QString& url, QLabel* statusLabel) {
    if (url.isEmpty()) {
        setStatus(statusLabel, "● offline", QColor(140, 140, 140));
        return;
    }
    auto* nam = new QNetworkAccessManager(statusLabel);
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(4000);
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, statusLabel, [this, reply, statusLabel, nam]() {
        if (reply->error() == QNetworkReply::NoError) {
            setStatus(statusLabel, "● online", QColor(80, 200, 100));
        } else {
            setStatus(statusLabel, "● offline", QColor(220, 90, 90));
        }
        reply->deleteLater();
        nam->deleteLater();
    });
}

void RemoteGpuPanel::onPingTick() {
    if (kokoroCheck_->isChecked()) {
        pingUrl(kokoroUrlEdit_->text().trimmed(), kokoroStatus_);
    } else {
        setStatus(kokoroStatus_, "● disabled", QColor(140, 140, 140));
    }
    if (fishCheck_->isChecked()) {
        pingUrl(fishUrlEdit_->text().trimmed(), fishStatus_);
    } else {
        setStatus(fishStatus_, "● disabled", QColor(140, 140, 140));
    }
}

void RemoteGpuPanel::onStopKokoroClicked() { emit stopKokoroRequested(); }
void RemoteGpuPanel::onStopFishClicked() { emit stopFishRequested(); }
void RemoteGpuPanel::onTestEdgeTtsClicked() {
    setEdgeTestInProgress(true);
    emit testEdgeTtsRequested();
}