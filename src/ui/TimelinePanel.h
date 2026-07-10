#pragma once

#include <deque>
#include <vector>

#include <QWidget>

#include "TimelineTypes.h"

class QListWidget;
class QListWidgetItem;
class QSpinBox;
class QLabel;
class QPushButton;

// Waveform timeline editor: visual overview, segment list with drag-reorder,
// per-segment pause/trim/fade controls, undo/redo, and click-to-seek.
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(QWidget* parent = nullptr);

    void setSegments(const std::vector<TimelineSegment>& segments);
    std::vector<TimelineSegment> segments() const;

    void setStatus(const QString& text);
    void setPlaybackPositionSeconds(double seconds);
    double playbackPositionForPaint() const;

    void handleWaveformClick(int x);
    double timeAtPixelX(int x) const;
    const std::vector<float>& waveformPeaksForPaint() const;
    const std::vector<TimelineSegment>& segmentsForPaint() const;
    int selectedIndexForPaint() const;

signals:
    void timelineChanged();
    void rebuildRequested();
    void rerenderSegmentRequested(int segmentIndex, const QString& text);
    void seekRequested(double seconds);

private slots:
    void onSegmentSelectionChanged();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onTimingChanged();
    void onTimingEditFinished();
    void onRebuildClicked();
    void onRerenderClicked();
    void onUndoClicked();
    void onRedoClicked();
    void onSegmentRowsMoved(const QModelIndex& parent, int start, int end, const QModelIndex& dest, int row);

private:
    void rebuildWaveformPeaks();
    void refreshSegmentList();
    void loadSelectedSegmentToEditor();
    void saveEditorToSelectedSegment();
    int segmentAtPixelX(int x) const;
    void selectSegment(int index);
    void pushUndoSnapshot();
    void syncSegmentsFromListOrder();
    void updateUndoButtons();

    std::vector<TimelineSegment> segments_;
    std::vector<float> waveformPeaks_;
    std::deque<std::vector<TimelineSegment>> undoStack_;
    std::deque<std::vector<TimelineSegment>> redoStack_;
    int selectedIndex_ = -1;

    QWidget* waveformWidget_ = nullptr;
    QListWidget* segmentList_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QSpinBox* pauseSpin_ = nullptr;
    QSpinBox* trimStartSpin_ = nullptr;
    QSpinBox* trimEndSpin_ = nullptr;
    QSpinBox* fadeInSpin_ = nullptr;
    QSpinBox* fadeOutSpin_ = nullptr;
    QPushButton* moveUpBtn_ = nullptr;
    QPushButton* moveDownBtn_ = nullptr;
    QPushButton* undoBtn_ = nullptr;
    QPushButton* redoBtn_ = nullptr;
    QPushButton* rebuildBtn_ = nullptr;
    QPushButton* rerenderBtn_ = nullptr;

    bool suppressEditorSignals_ = false;
    bool suppressListReorder_ = false;
    double playbackPositionSec_ = -1.0;

    static constexpr int kMaxUndo = 20;
};