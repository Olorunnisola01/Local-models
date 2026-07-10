#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

// Unified remote GPU controls for Kokoro and Fish Audio Kaggle servers.
class RemoteGpuPanel : public QWidget {
    Q_OBJECT
public:
    explicit RemoteGpuPanel(QWidget* parent = nullptr);

    bool kokoroEnabled() const;
    QString kokoroUrl() const;
    bool fishEnabled() const;
    QString fishUrl() const;

    void loadFromSettings();
    void setEdgeTestInProgress(bool inProgress);
    void setEdgeTestResult(bool ok, const QString& message);
    void saveToSettings() const;
    QString statusSummary() const;

signals:
    void stopKokoroRequested();
    void stopFishRequested();
    void settingsChanged();
    void statusChanged();
    void testEdgeTtsRequested();
    void kokoroToggled(bool checked);

private slots:
    void onPingTick();
    void onStopKokoroClicked();
    void onStopFishClicked();
    void onTestEdgeTtsClicked();

private:
    void setStatus(QLabel* label, const QString& text, const QColor& color);
    void pingUrl(const QString& url, QLabel* statusLabel);

    QCheckBox* kokoroCheck_ = nullptr;
    QLineEdit* kokoroUrlEdit_ = nullptr;
    QLabel* kokoroStatus_ = nullptr;
    QPushButton* stopKokoroBtn_ = nullptr;
    QLabel* kokoroHint_ = nullptr;

    QWidget* fishSection_ = nullptr;
    QCheckBox* fishCheck_ = nullptr;
    QLineEdit* fishUrlEdit_ = nullptr;
    QLabel* fishStatus_ = nullptr;
    QPushButton* stopFishBtn_ = nullptr;

    QPushButton* testEdgeBtn_ = nullptr;
    QLabel* edgeStatus_ = nullptr;

    QTimer* pingTimer_ = nullptr;
};