#include "VexaraEditor/FindBar.h"

#include "VexaraEditor/TextContextMenu.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace VexaraEditor {

FindBar::FindBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    auto* findRow = new QHBoxLayout();
    findRow->addWidget(new QLabel(QStringLiteral("Find"), this));
    findField_ = new QLineEdit(this);
    findField_->setPlaceholderText(QStringLiteral("Search in file"));
    findField_->setClearButtonEnabled(true);
    previousButton_ = new QPushButton(QStringLiteral("Prev"), this);
    nextButton_ = new QPushButton(QStringLiteral("Next"), this);
    findRow->addWidget(findField_, 1);
    findRow->addWidget(previousButton_);
    findRow->addWidget(nextButton_);

    auto* replaceRow = new QHBoxLayout();
    replaceRow->addWidget(new QLabel(QStringLiteral("Replace"), this));
    replaceField_ = new QLineEdit(this);
    replaceField_->setPlaceholderText(QStringLiteral("Replacement text"));
    replaceField_->setClearButtonEnabled(true);
    replaceButton_ = new QPushButton(QStringLiteral("Replace"), this);
    replaceAllButton_ = new QPushButton(QStringLiteral("All"), this);
    replaceRow->addWidget(replaceField_, 1);
    replaceRow->addWidget(replaceButton_);
    replaceRow->addWidget(replaceAllButton_);

    layout->addLayout(findRow);
    layout->addLayout(replaceRow);

    connect(nextButton_, &QPushButton::clicked, this, &FindBar::findNextRequested);
    connect(previousButton_, &QPushButton::clicked, this, &FindBar::findPreviousRequested);
    connect(findField_, &QLineEdit::returnPressed, this, &FindBar::findNextRequested);

    connect(replaceButton_, &QPushButton::clicked, this, &FindBar::replaceRequested);
    connect(replaceAllButton_, &QPushButton::clicked, this, &FindBar::replaceAllRequested);
    connect(replaceField_, &QLineEdit::returnPressed, this, &FindBar::replaceRequested);

    installLineEditContextMenu(findField_);
    installLineEditContextMenu(replaceField_);
}

QString FindBar::query() const
{
    return findField_->text();
}

QString FindBar::replacement() const
{
    return replaceField_->text();
}

void FindBar::focusQuery()
{
    findField_->setFocus();
    findField_->selectAll();
}

void FindBar::focusReplace()
{
    replaceField_->setFocus();
    replaceField_->selectAll();
}

} // namespace VexaraEditor
