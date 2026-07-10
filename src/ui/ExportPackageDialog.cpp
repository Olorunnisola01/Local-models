#include "ExportPackageDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

ExportPackageDialog::ExportPackageDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Export Package");
    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(
        "Export a production folder with combined audio, per-segment WAVs, and timed captions.",
        this));

    combinedWavCheck_ = new QCheckBox("Combined WAV (EQ + normalized)", this);
    combinedWavCheck_->setChecked(true);
    segmentWavsCheck_ = new QCheckBox("Per-segment WAV files (/segments)", this);
    segmentWavsCheck_->setChecked(true);
    srtCheck_ = new QCheckBox("Subtitles — captions.srt", this);
    srtCheck_->setChecked(true);
    vttCheck_ = new QCheckBox("Subtitles — captions.vtt", this);
    vttCheck_->setChecked(true);
    manifestCheck_ = new QCheckBox("Manifest — manifest.json (timestamps + text)", this);
    manifestCheck_->setChecked(true);

    layout->addWidget(combinedWavCheck_);
    layout->addWidget(segmentWavsCheck_);
    layout->addWidget(srtCheck_);
    layout->addWidget(vttCheck_);
    layout->addWidget(manifestCheck_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

captions::ExportPackageOptions ExportPackageDialog::options() const {
    captions::ExportPackageOptions opts;
    opts.combinedWav = combinedWavCheck_->isChecked();
    opts.segmentWavs = segmentWavsCheck_->isChecked();
    opts.srt = srtCheck_->isChecked();
    opts.vtt = vttCheck_->isChecked();
    opts.manifest = manifestCheck_->isChecked();
    return opts;
}