#pragma once

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// In-app model status checker and script launcher.
class ModelManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ModelManagerDialog(QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();
    void onSyncSupertonicClicked();
    void onFetchOnnxClicked();
    void onFetchKokoroClicked();
    void onFetchPiperClicked();
    void onFetchDfnClicked();
    void onOpenModelsFolderClicked();
    void onProcessFinished(int exitCode);

private:
    void appendLog(const QString& line);
    void refreshStatus();
    QString resolveScriptPath(const QString& scriptName) const;
    void runScript(const QString& scriptName);

    QLabel* statusLabel_ = nullptr;
    QPlainTextEdit* logEdit_ = nullptr;
    QPushButton* syncBtn_ = nullptr;
    QPushButton* fetchBtn_ = nullptr;
    class QProcess* process_ = nullptr;
};