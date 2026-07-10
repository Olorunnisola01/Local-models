#pragma once

#include <QComboBox>
#include <QCompleter>

namespace tts {

// Gives a QComboBox the same polished dropdown the Single Speaker voice picker
// uses: a fixed-height popup that scrolls neatly (with a scrollbar) instead of
// an oversized native popup that can span the whole screen.
//
//  - `combobox-popup: 0` is the key: it forces Qt to honor setMaxVisibleItems()
//    and render the popup as a scrollable item view.
//  - When `searchable` is true the combo also becomes type-to-filter (case-
//    insensitive "contains"), matching setupVoiceCompleter() for voice lists.
inline void applyNeatComboPopup(QComboBox* combo, bool searchable, int maxVisible = 15) {
    if (combo == nullptr) {
        return;
    }
    combo->setMaxVisibleItems(maxVisible);
    // Append so we don't clobber any existing stylesheet on the combo.
    const QString existing = combo->styleSheet();
    combo->setStyleSheet(existing + QStringLiteral("\nQComboBox { combobox-popup: 0; }"));

    if (searchable) {
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        auto* completer = new QCompleter(combo);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        combo->setCompleter(completer);
    }
}

} // namespace tts
