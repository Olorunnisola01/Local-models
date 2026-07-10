#pragma once

#include <QWidget>

class QLabel;

// Provider-aware inline hints for supported tags and tips.
class ProviderHintsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProviderHintsPanel(QWidget* parent = nullptr);

    void setProviderIndex(int providerIndex);

private:
    QLabel* hintLabel_ = nullptr;
};