#include "ProviderHintsPanel.h"

#include <QLabel>
#include <QVBoxLayout>

ProviderHintsPanel::ProviderHintsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    hintLabel_ = new QLabel(this);
    hintLabel_->setWordWrap(true);
    hintLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(hintLabel_);
    setProviderIndex(0);
}

void ProviderHintsPanel::setProviderIndex(int providerIndex) {
    QString html;
    switch (providerIndex) {
        case 3:
            html = "<b>Edge TTS:</b> Use <tt>[emph]word[/emph]</tt> or <tt>*word*</tt> for emphasis. "
                   "<tt>[pause=400ms]</tt> for inline pauses. Enable <b>Natural Humanizer</b> for best quality.";
            break;
        case 4:
            html = "<b>Fish Audio S2:</b> Place emotion tags at sentence start — "
                   "<tt>[happy]</tt> <tt>[sad]</tt> <tt>[whispering]</tt> <tt>[laughing]</tt>. "
                   "Combine up to 3 tags. See <b>Read me</b> for the full list.";
            break;
        case 2:
            html = "<b>Piper (German):</b> <tt>[pause=…]</tt> tags work. Use the <b>Pronunciation</b> tab for names. "
                   "Emphasis/Fish tags are stripped.";
            break;
        case 1:
            html = "<b>Kokoro:</b> <tt>[pause=…]</tt> inline pauses supported. Voice mixing available. "
                   "Use remote Kaggle GPU for faster renders on long text.";
            break;
        default:
            html = "<b>Supertonic:</b> <tt>[pause=…]</tt> inline pauses supported. "
                   "Mix two voices for blended output. Tune sentence gaps in <b>Settings…</b>";
            break;
    }
    hintLabel_->setText(html);
}