#pragma once

#include <QDialog>

class QLabel;

// First-launch health check: models, GPU, remote URLs, link to Read me.
class FirstRunWizard : public QDialog {
    Q_OBJECT
public:
    explicit FirstRunWizard(const QString& modelDir, QWidget* parent = nullptr);

    static bool shouldShow();
    static void markComplete();

private:
    QLabel* checksLabel_ = nullptr;
};