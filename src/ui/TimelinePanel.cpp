#include "TimelinePanel.h"

#include <algorithm>

#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QAbstractItemModel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "AudioTimelineBuilder.h"

namespace {

constexpr int kWaveformHeight = 120;
constexpr int kWaveformPoints = 1200;

QColor colorForSegment(int index) {
    static const QColor palette[] = {
        QColor(70, 130, 220),  QColor(220, 110, 70),  QColor(90, 180, 90),
        QColor(180, 90, 180),  QColor(220, 180, 60),  QColor(60, 180, 200),
    };
    return palette[index % 6];
}

QString formatSegmentLabel(const TimelineSegment& seg, int index) {
    const QString label = QString::fromStdString(seg.label);
    const double seconds =
        seg.sampleRate > 0 ? static_cast<double>(seg.samples.size()) / seg.sampleRate : 0.0;
    return QString("%1. %2 (%3s, pause %4ms)")
        .arg(index + 1)
        .arg(label.isEmpty() ? "Segment" : label)
        .arg(seconds, 0, 'f', 1)
        .arg(seg.pauseAfterMs);
}

class WaveformCanvas : public QWidget {
public:
    explicit WaveformCanvas(TimelinePanel* owner) : QWidget(owner), owner_(owner) {
        setMinimumHeight(kWaveformHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(28, 28, 32));

        const std::vector<float>& peaks = owner_->waveformPeaksForPaint();
        const std::vector<TimelineSegment>& segments = owner_->segmentsForPaint();
        const int selected = owner_->selectedIndexForPaint();

        if (!peaks.empty()) {
            painter.setPen(QColor(90, 160, 255, 180));
            const int midY = height() / 2;
            const float xScale = static_cast<float>(width()) / static_cast<float>(peaks.size());
            for (size_t i = 1; i < peaks.size(); ++i) {
                const int x0 = static_cast<int>((i - 1) * xScale);
                const int x1 = static_cast<int>(i * xScale);
                const int y0 = midY - static_cast<int>(peaks[i - 1] * (midY - 4));
                const int y1 = midY - static_cast<int>(peaks[i] * (midY - 4));
                painter.drawLine(x0, y0, x1, y1);
                painter.drawLine(x0, midY + (midY - y0), x1, midY + (midY - y1));
            }
        }

        if (segments.empty()) {
            painter.setPen(QColor(140, 140, 150));
            painter.drawText(rect(), Qt::AlignCenter, "No timeline segments yet");
            return;
        }

        tts::AudioBuffer preview = timeline::buildFromTimeline(segments);
        if (preview.samples.empty() || preview.sampleRate <= 0) {
            return;
        }
        const double totalSeconds =
            static_cast<double>(preview.samples.size()) / preview.sampleRate;
        double elapsed = 0.0;
        for (size_t i = 0; i < segments.size(); ++i) {
            const TimelineSegment& seg = segments[i];
            std::vector<float> chunk = timeline::extractTrimmed(seg);
            const double segSeconds =
                seg.sampleRate > 0 ? static_cast<double>(chunk.size()) / seg.sampleRate : 0.0;
            const int x0 = static_cast<int>((elapsed / totalSeconds) * width());
            elapsed += segSeconds;
            const int x1 = static_cast<int>((elapsed / totalSeconds) * width());

            QColor fill = colorForSegment(static_cast<int>(i));
            fill.setAlpha(static_cast<int>(i) == selected ? 110 : 70);
            painter.fillRect(x0, 2, std::max(2, x1 - x0), height() - 4, fill);

            if (static_cast<int>(i) == selected) {
                painter.setPen(QPen(QColor(255, 220, 80), 2));
                painter.drawRect(x0 + 1, 3, std::max(1, x1 - x0 - 2), height() - 6);
            }

            if (seg.pauseAfterMs > 0) {
                elapsed += seg.pauseAfterMs / 1000.0;
            }
        }

        const double playPos = owner_->playbackPositionForPaint();
        if (playPos >= 0.0 && totalSeconds > 0.0) {
            const int playX = static_cast<int>((playPos / totalSeconds) * width());
            painter.setPen(QPen(QColor(255, 80, 80), 2));
            painter.drawLine(playX, 0, playX, height());
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            owner_->handleWaveformClick(event->pos().x());
        }
        QWidget::mousePressEvent(event);
    }

private:
    TimelinePanel* owner_;
};

} // namespace

TimelinePanel::TimelinePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* headerRow = new QHBoxLayout();
    headerRow->addWidget(new QLabel(
        "Timeline — click waveform to seek; drag segments to reorder:", this));
    undoBtn_ = new QPushButton("Undo", this);
    redoBtn_ = new QPushButton("Redo", this);
    undoBtn_->setEnabled(false);
    redoBtn_->setEnabled(false);
    headerRow->addStretch(1);
    headerRow->addWidget(undoBtn_);
    headerRow->addWidget(redoBtn_);
    layout->addLayout(headerRow);

    waveformWidget_ = new WaveformCanvas(this);
    layout->addWidget(waveformWidget_);

    auto* middleRow = new QHBoxLayout();

    segmentList_ = new QListWidget(this);
    segmentList_->setMinimumWidth(260);
    segmentList_->setDragDropMode(QAbstractItemView::InternalMove);
    segmentList_->setDefaultDropAction(Qt::MoveAction);
    segmentList_->setSelectionMode(QAbstractItemView::SingleSelection);
    middleRow->addWidget(segmentList_, 1);

    auto* inspector = new QGroupBox("Selected Segment", this);
    auto* form = new QFormLayout(inspector);

    pauseSpin_ = new QSpinBox(inspector);
    pauseSpin_->setRange(0, 5000);
    pauseSpin_->setSuffix(" ms");
    form->addRow("Pause after:", pauseSpin_);

    trimStartSpin_ = new QSpinBox(inspector);
    trimStartSpin_->setRange(0, 10000);
    trimStartSpin_->setSuffix(" ms");
    form->addRow("Trim start:", trimStartSpin_);

    trimEndSpin_ = new QSpinBox(inspector);
    trimEndSpin_->setRange(0, 10000);
    trimEndSpin_->setSuffix(" ms");
    form->addRow("Trim end:", trimEndSpin_);

    fadeInSpin_ = new QSpinBox(inspector);
    fadeInSpin_->setRange(0, 3000);
    fadeInSpin_->setSuffix(" ms");
    form->addRow("Fade in:", fadeInSpin_);

    fadeOutSpin_ = new QSpinBox(inspector);
    fadeOutSpin_->setRange(0, 3000);
    fadeOutSpin_->setSuffix(" ms");
    form->addRow("Fade out:", fadeOutSpin_);

    auto* orderRow = new QHBoxLayout();
    moveUpBtn_ = new QPushButton("Move Up", inspector);
    moveDownBtn_ = new QPushButton("Move Down", inspector);
    orderRow->addWidget(moveUpBtn_);
    orderRow->addWidget(moveDownBtn_);
    form->addRow("Order:", orderRow);

    rebuildBtn_ = new QPushButton("Apply Timeline to Audio", inspector);
    rerenderBtn_ = new QPushButton("Re-render Selected Segment", inspector);
    rerenderBtn_->setToolTip("Re-synthesize only the selected timeline segment with current voice settings");
    form->addRow(rebuildBtn_);
    form->addRow(rerenderBtn_);

    middleRow->addWidget(inspector, 1);
    layout->addLayout(middleRow);

    statusLabel_ = new QLabel("Synthesize or render dialogue to populate the timeline.", this);
    layout->addWidget(statusLabel_);

    connect(segmentList_, &QListWidget::currentRowChanged, this, &TimelinePanel::onSegmentSelectionChanged);
    connect(moveUpBtn_, &QPushButton::clicked, this, &TimelinePanel::onMoveUpClicked);
    connect(moveDownBtn_, &QPushButton::clicked, this, &TimelinePanel::onMoveDownClicked);
    connect(rebuildBtn_, &QPushButton::clicked, this, &TimelinePanel::onRebuildClicked);
    connect(rerenderBtn_, &QPushButton::clicked, this, &TimelinePanel::onRerenderClicked);
    connect(undoBtn_, &QPushButton::clicked, this, &TimelinePanel::onUndoClicked);
    connect(redoBtn_, &QPushButton::clicked, this, &TimelinePanel::onRedoClicked);
    connect(segmentList_->model(), &QAbstractItemModel::rowsMoved, this, &TimelinePanel::onSegmentRowsMoved);

    auto connectSpin = [this](QSpinBox* spin) {
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &TimelinePanel::onTimingChanged);
        connect(spin, &QSpinBox::editingFinished, this, &TimelinePanel::onTimingEditFinished);
    };
    connectSpin(pauseSpin_);
    connectSpin(trimStartSpin_);
    connectSpin(trimEndSpin_);
    connectSpin(fadeInSpin_);
    connectSpin(fadeOutSpin_);
}

void TimelinePanel::setSegments(const std::vector<TimelineSegment>& segments) {
    segments_ = segments;
    selectedIndex_ = segments_.empty() ? -1 : 0;
    undoStack_.clear();
    redoStack_.clear();
    updateUndoButtons();
    rebuildWaveformPeaks();
    refreshSegmentList();
    if (!segments_.empty()) {
        segmentList_->setCurrentRow(0);
    }
    waveformWidget_->update();
}

std::vector<TimelineSegment> TimelinePanel::segments() const {
    return segments_;
}

void TimelinePanel::setStatus(const QString& text) {
    statusLabel_->setText(text);
}

void TimelinePanel::rebuildWaveformPeaks() {
    tts::AudioBuffer preview = timeline::buildFromTimeline(segments_);
    waveformPeaks_ = timeline::computeWaveformPeaks(preview.samples, kWaveformPoints);
}

void TimelinePanel::refreshSegmentList() {
    const int previous = segmentList_->currentRow();
    suppressListReorder_ = true;
    segmentList_->clear();
    for (int i = 0; i < static_cast<int>(segments_.size()); ++i) {
        auto* item = new QListWidgetItem(formatSegmentLabel(segments_[static_cast<size_t>(i)], i));
        item->setData(Qt::UserRole, i);
        segmentList_->addItem(item);
    }
    suppressListReorder_ = false;
    if (!segments_.empty()) {
        segmentList_->setCurrentRow(std::clamp(previous, 0, static_cast<int>(segments_.size()) - 1));
    }
}

void TimelinePanel::selectSegment(int index) {
    if (index < 0 || index >= static_cast<int>(segments_.size())) {
        return;
    }
    selectedIndex_ = index;
    segmentList_->setCurrentRow(index);
}

void TimelinePanel::loadSelectedSegmentToEditor() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(segments_.size())) {
        return;
    }
    const TimelineSegment& seg = segments_[static_cast<size_t>(selectedIndex_)];
    suppressEditorSignals_ = true;
    pauseSpin_->setValue(seg.pauseAfterMs);
    trimStartSpin_->setValue(seg.trimStartMs);
    trimEndSpin_->setValue(seg.trimEndMs);
    fadeInSpin_->setValue(seg.fadeInMs);
    fadeOutSpin_->setValue(seg.fadeOutMs);
    suppressEditorSignals_ = false;

    moveUpBtn_->setEnabled(selectedIndex_ > 0);
    moveDownBtn_->setEnabled(selectedIndex_ + 1 < static_cast<int>(segments_.size()));
}

void TimelinePanel::saveEditorToSelectedSegment() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(segments_.size())) {
        return;
    }
    TimelineSegment& seg = segments_[static_cast<size_t>(selectedIndex_)];
    seg.pauseAfterMs = pauseSpin_->value();
    seg.trimStartMs = trimStartSpin_->value();
    seg.trimEndMs = trimEndSpin_->value();
    seg.fadeInMs = fadeInSpin_->value();
    seg.fadeOutMs = fadeOutSpin_->value();
}

void TimelinePanel::onSegmentSelectionChanged() {
    selectedIndex_ = segmentList_->currentRow();
    loadSelectedSegmentToEditor();
    waveformWidget_->update();
}

void TimelinePanel::pushUndoSnapshot() {
    undoStack_.push_back(segments_);
    if (undoStack_.size() > kMaxUndo) {
        undoStack_.pop_front();
    }
    redoStack_.clear();
    updateUndoButtons();
}

void TimelinePanel::syncSegmentsFromListOrder() {
    std::vector<TimelineSegment> reordered;
    reordered.reserve(segments_.size());
    for (int row = 0; row < segmentList_->count(); ++row) {
        const int oldIndex = segmentList_->item(row)->data(Qt::UserRole).toInt();
        if (oldIndex >= 0 && oldIndex < static_cast<int>(segments_.size())) {
            reordered.push_back(segments_[static_cast<size_t>(oldIndex)]);
        }
    }
    if (reordered.size() == segments_.size()) {
        segments_ = std::move(reordered);
    }
}

void TimelinePanel::updateUndoButtons() {
    if (undoBtn_) {
        undoBtn_->setEnabled(!undoStack_.empty());
    }
    if (redoBtn_) {
        redoBtn_->setEnabled(!redoStack_.empty());
    }
}

void TimelinePanel::onUndoClicked() {
    if (undoStack_.empty()) {
        return;
    }
    redoStack_.push_back(segments_);
    segments_ = undoStack_.back();
    undoStack_.pop_back();
    selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(segments_.size()) - 1);
    updateUndoButtons();
    rebuildWaveformPeaks();
    refreshSegmentList();
    segmentList_->setCurrentRow(selectedIndex_);
    emit timelineChanged();
    waveformWidget_->update();
}

void TimelinePanel::onRedoClicked() {
    if (redoStack_.empty()) {
        return;
    }
    undoStack_.push_back(segments_);
    segments_ = redoStack_.back();
    redoStack_.pop_back();
    selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(segments_.size()) - 1);
    updateUndoButtons();
    rebuildWaveformPeaks();
    refreshSegmentList();
    segmentList_->setCurrentRow(selectedIndex_);
    emit timelineChanged();
    waveformWidget_->update();
}

void TimelinePanel::onSegmentRowsMoved(const QModelIndex& /*parent*/, int /*start*/, int /*end*/,
                                         const QModelIndex& /*dest*/, int /*row*/) {
    if (suppressListReorder_) {
        return;
    }
    pushUndoSnapshot();
    syncSegmentsFromListOrder();
    selectedIndex_ = segmentList_->currentRow();
    rebuildWaveformPeaks();
    refreshSegmentList();
    segmentList_->setCurrentRow(selectedIndex_);
    emit timelineChanged();
    waveformWidget_->update();
}

void TimelinePanel::onMoveUpClicked() {
    if (selectedIndex_ <= 0) {
        return;
    }
    pushUndoSnapshot();
    std::swap(segments_[static_cast<size_t>(selectedIndex_)],
              segments_[static_cast<size_t>(selectedIndex_ - 1)]);
    --selectedIndex_;
    rebuildWaveformPeaks();
    refreshSegmentList();
    segmentList_->setCurrentRow(selectedIndex_);
    emit timelineChanged();
    waveformWidget_->update();
}

void TimelinePanel::onMoveDownClicked() {
    if (selectedIndex_ < 0 || selectedIndex_ + 1 >= static_cast<int>(segments_.size())) {
        return;
    }
    pushUndoSnapshot();
    std::swap(segments_[static_cast<size_t>(selectedIndex_)],
              segments_[static_cast<size_t>(selectedIndex_ + 1)]);
    ++selectedIndex_;
    rebuildWaveformPeaks();
    refreshSegmentList();
    segmentList_->setCurrentRow(selectedIndex_);
    emit timelineChanged();
    waveformWidget_->update();
}

void TimelinePanel::onTimingChanged() {
    if (suppressEditorSignals_) {
        return;
    }
    saveEditorToSelectedSegment();
    rebuildWaveformPeaks();
    refreshSegmentList();
    segmentList_->setCurrentRow(selectedIndex_);
    emit timelineChanged();
    waveformWidget_->update();
}

void TimelinePanel::onTimingEditFinished() {
    if (suppressEditorSignals_) {
        return;
    }
    pushUndoSnapshot();
}

void TimelinePanel::onRebuildClicked() {
    saveEditorToSelectedSegment();
    emit rebuildRequested();
}

void TimelinePanel::onRerenderClicked() {
    saveEditorToSelectedSegment();
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(segments_.size())) {
        setStatus("Select a segment to re-render.");
        return;
    }
    emit rerenderSegmentRequested(selectedIndex_, QString::fromStdString(segments_[selectedIndex_].text));
}

int TimelinePanel::segmentAtPixelX(int x) const {
    if (segments_.empty() || waveformWidget_->width() <= 0) {
        return -1;
    }

    tts::AudioBuffer preview = timeline::buildFromTimeline(segments_);
    if (preview.samples.empty() || preview.sampleRate <= 0) {
        return -1;
    }

    const double totalSeconds =
        static_cast<double>(preview.samples.size()) / static_cast<double>(preview.sampleRate);
    if (totalSeconds <= 0.0) {
        return -1;
    }

    const double clickSeconds = (static_cast<double>(x) / waveformWidget_->width()) * totalSeconds;
    double elapsed = 0.0;
    for (size_t i = 0; i < segments_.size(); ++i) {
        const TimelineSegment& seg = segments_[i];
        std::vector<float> chunk = timeline::extractTrimmed(seg);
        const double segSeconds =
            seg.sampleRate > 0 ? static_cast<double>(chunk.size()) / seg.sampleRate : 0.0;
        if (clickSeconds >= elapsed && clickSeconds < elapsed + segSeconds) {
            return static_cast<int>(i);
        }
        elapsed += segSeconds;
        if (seg.pauseAfterMs > 0) {
            elapsed += seg.pauseAfterMs / 1000.0;
        }
    }
    return static_cast<int>(segments_.size()) - 1;
}

double TimelinePanel::timeAtPixelX(int x) const {
    if (segments_.empty() || waveformWidget_->width() <= 0) {
        return 0.0;
    }
    tts::AudioBuffer preview = timeline::buildFromTimeline(segments_);
    if (preview.samples.empty() || preview.sampleRate <= 0) {
        return 0.0;
    }
    const double totalSeconds =
        static_cast<double>(preview.samples.size()) / static_cast<double>(preview.sampleRate);
    return (static_cast<double>(x) / waveformWidget_->width()) * totalSeconds;
}

void TimelinePanel::handleWaveformClick(int x) {
    const int index = segmentAtPixelX(x);
    if (index >= 0) {
        selectSegment(index);
    }
    emit seekRequested(timeAtPixelX(x));
}

const std::vector<float>& TimelinePanel::waveformPeaksForPaint() const {
    return waveformPeaks_;
}

const std::vector<TimelineSegment>& TimelinePanel::segmentsForPaint() const {
    return segments_;
}

int TimelinePanel::selectedIndexForPaint() const {
    return selectedIndex_;
}

void TimelinePanel::setPlaybackPositionSeconds(double seconds) {
    playbackPositionSec_ = seconds;
    if (waveformWidget_) {
        waveformWidget_->update();
    }
}

double TimelinePanel::playbackPositionForPaint() const {
    return playbackPositionSec_;
}