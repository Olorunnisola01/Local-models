#pragma once

#include <QDialog>

// Scrollable guide to inline text tags, markup, and copy-paste examples.
class ReadMeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ReadMeDialog(QWidget* parent = nullptr);
};