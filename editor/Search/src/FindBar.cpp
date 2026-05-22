#include "VexaraEditor/FindBar.h"

#include "VexaraEditor/TextContextMenu.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

namespace VexaraEditor {

FindBar::FindBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);

    queryField_ = new QLineEdit(this);
    queryField_->setPlaceholderText(QStringLiteral("Find in file"));
    queryField_->setClearButtonEnabled(true);

    previousButton_ = new QPushButton(QStringLiteral("Previous"), this);
    nextButton_ = new QPushButton(QStringLiteral("Next"), this);

    layout->addWidget(queryField_, 1);
    layout->addWidget(previousButton_);
    layout->addWidget(nextButton_);

    connect(nextButton_, &QPushButton::clicked, this, &FindBar::findNextRequested);
    connect(previousButton_, &QPushButton::clicked, this, &FindBar::findPreviousRequested);
    connect(queryField_, &QLineEdit::returnPressed, this, &FindBar::findNextRequested);

    installLineEditContextMenu(queryField_);
}

QString FindBar::query() const
{
    return queryField_->text();
}

void FindBar::focusQuery()
{
    queryField_->setFocus();
    queryField_->selectAll();
}

} // namespace VexaraEditor
