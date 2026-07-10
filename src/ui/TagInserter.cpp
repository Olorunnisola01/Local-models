#include "TagInserter.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>

TagInserter::TagInserter(QPlainTextEdit* target, QWidget* parent)
    : QWidget(parent), target_(target) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(new QLabel("Insert tag:", this));
    tagCombo_ = new QComboBox(this);
    tagCombo_->setMinimumWidth(200);
    row->addWidget(tagCombo_, 1);
    insertBtn_ = new QPushButton("Insert", this);
    row->addWidget(insertBtn_);
    connect(insertBtn_, &QPushButton::clicked, this, &TagInserter::onInsertClicked);
    rebuildTags();
}

void TagInserter::setProviderIndex(int providerIndex) {
    providerIndex_ = providerIndex;
    rebuildTags();
}

void TagInserter::rebuildTags() {
    tagCombo_->clear();
    tagCombo_->addItem("[pause=300ms]", "[pause=300ms]");
    tagCombo_->addItem("[pause=600ms]", "[pause=600ms]");
    tagCombo_->addItem("[pause=1000ms]", "[pause=1000ms]");

    // Provider index: 0 Supertonic, 1 Kokoro, 2 Piper, 3 Edge, 4 Fish
    if (providerIndex_ == 3) {
        tagCombo_->addItem("[emph]word[/emph]", "[emph]word[/emph]");
        tagCombo_->addItem("*emphasis*", "*word*");
    }
    if (providerIndex_ == 4) {
        tagCombo_->addItems({"[happy]", "[sad]", "[angry]", "[excited]", "[whispering]",
                             "[laughing]", "[break]", "[long-break]"});
        for (int i = tagCombo_->count() - 8; i < tagCombo_->count(); ++i) {
            const QString t = tagCombo_->itemText(i);
            tagCombo_->setItemData(i, t);
        }
    }
}

void TagInserter::onInsertClicked() {
    if (!target_) {
        return;
    }
    const QString tag = tagCombo_->currentData().toString().isEmpty()
                            ? tagCombo_->currentText()
                            : tagCombo_->currentData().toString();
    QTextCursor cursor = target_->textCursor();
    cursor.insertText(tag);
    target_->setTextCursor(cursor);
    target_->setFocus();
    emit tagInserted(tag);
}