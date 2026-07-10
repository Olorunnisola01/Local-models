#pragma once

#include <QDialog>

#include "CaptionExporter.h"

class QCheckBox;

class ExportPackageDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportPackageDialog(QWidget* parent = nullptr);

    captions::ExportPackageOptions options() const;

private:
    QCheckBox* combinedWavCheck_ = nullptr;
    QCheckBox* segmentWavsCheck_ = nullptr;
    QCheckBox* srtCheck_ = nullptr;
    QCheckBox* vttCheck_ = nullptr;
    QCheckBox* manifestCheck_ = nullptr;
};