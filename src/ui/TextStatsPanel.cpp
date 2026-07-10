#include "TextStatsPanel.h"

#include <QHBoxLayout>
#include <QRegularExpression>
#include <QStringList>

namespace {

int countWords(const QString& text) {
    const QStringList parts =
        text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    return parts.size();
}

int estimateChunks(const QString& text, int maxChunkChars) {
    if (text.isEmpty()) {
        return 0;
    }
    int chunks = 0;
    int currentLen = 0;
    const QStringList paragraphs = text.split(QStringLiteral("\n\n"), Qt::KeepEmptyParts);
    for (const QString& para : paragraphs) {
        const QString trimmed = para.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (trimmed.size() <= maxChunkChars) {
            ++chunks;
            continue;
        }
        chunks += (trimmed.size() + maxChunkChars - 1) / maxChunkChars;
    }
    if (chunks == 0) {
        chunks = (text.size() + maxChunkChars - 1) / std::max(1, maxChunkChars);
    }
    return std::max(1, chunks);
}

int sumInlinePauseMs(const QString& text) {
    static const QRegularExpression pauseRe(QStringLiteral(R"(\[pause(?:\s*=\s*|\s+)(\d+)\s*(?:ms)?\])"),
                                            QRegularExpression::CaseInsensitiveOption);
    int total = 0;
    auto it = pauseRe.globalMatch(text);
    while (it.hasNext()) {
        total += it.next().captured(1).toInt();
    }
    return total;
}

} // namespace

TextStatsPanel::TextStatsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    statsLabel_ = new QLabel(this);
    statsLabel_->setStyleSheet("color: palette(mid);");
    layout->addWidget(statsLabel_);
    updateStats(QString(), 1.0f, 400, 150, 600);
}

void TextStatsPanel::updateStats(const QString& text, float speed, int maxChunkChars, int sentenceGapMs,
                                 int paragraphGapMs) {
    const int chars = text.size();
    const int words = countWords(text);
    const int chunks = estimateChunks(text, maxChunkChars);

    const double wordsPerSec = 2.5 * static_cast<double>(speed);
    double estSec = wordsPerSec > 0.0 ? static_cast<double>(words) / wordsPerSec : 0.0;

    const int sentenceEnds =
        text.count(QRegularExpression(QStringLiteral("[.!?]")));
    estSec += static_cast<double>(sentenceEnds * sentenceGapMs) / 1000.0;

    const int paragraphBreaks = text.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts).size();
    if (paragraphBreaks > 1) {
        estSec += static_cast<double>((paragraphBreaks - 1) * paragraphGapMs) / 1000.0;
    }
    estSec += static_cast<double>(sumInlinePauseMs(text)) / 1000.0;

    statsLabel_->setText(
        QString("Chars: %1  |  Words: %2  |  Est. ~%3s  |  ~%4 chunk(s)")
            .arg(chars)
            .arg(words)
            .arg(estSec, 0, 'f', 1)
            .arg(chunks));
}