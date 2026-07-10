#pragma once

#include <QLabel>
#include <QWidget>

// Live character/word count, estimated duration, and chunk estimate.
class TextStatsPanel : public QWidget {
    Q_OBJECT
public:
    explicit TextStatsPanel(QWidget* parent = nullptr);

    void updateStats(const QString& text, float speed, int maxChunkChars, int sentenceGapMs,
                     int paragraphGapMs);

private:
    QLabel* statsLabel_ = nullptr;
};