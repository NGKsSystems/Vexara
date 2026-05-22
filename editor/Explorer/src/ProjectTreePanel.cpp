#include "VexaraEditor/ProjectTreePanel.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>

namespace VexaraEditor {

namespace {

void hideExtraColumns(QTreeView* tree, QFileSystemModel* model)
{
    for (int column = 1; column < model->columnCount(); ++column) {
        tree->setColumnHidden(column, true);
    }
}

void tuneNameColumn(QTreeView* tree)
{
    QHeaderView* header = tree->header();
    header->setVisible(true);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(200);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
}

} // namespace

ProjectTreePanel::ProjectTreePanel(QWidget* parent)
    : QWidget(parent)
{
    model_ = new QFileSystemModel(this);
    model_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);

    placeholder_ = new QLabel(
        QStringLiteral("No folder opened.\n\nUse File > Open Folder\nor the Open menu on the toolbar."),
        this);
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);

    tree_ = new QTreeView(this);
    tree_->setModel(model_);
    tree_->setAnimated(false);
    tree_->setIndentation(18);
    tree_->setTextElideMode(Qt::ElideNone);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tree_->setUniformRowHeights(true);
    tree_->setWordWrap(false);
    tree_->setMinimumWidth(240);

    hideExtraColumns(tree_, model_);
    tuneNameColumn(tree_);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(placeholder_);
    stack_->addWidget(tree_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        const QFileInfo info(model_->filePath(index));
        if (info.isFile()) {
            emit fileActivated(info.absoluteFilePath());
        }
    });

    connect(tree_, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        tree_->setToolTip(model_->filePath(index));
    });

    connect(tree_, &QTreeView::entered, this, [this](const QModelIndex& index) {
        tree_->setToolTip(model_->filePath(index));
    });

    showPlaceholder();
}

void ProjectTreePanel::setRootPath(const QString& path)
{
    const QModelIndex rootIndex = model_->setRootPath(path);
    tree_->setRootIndex(rootIndex);
    hideExtraColumns(tree_, model_);
    tuneNameColumn(tree_);
    tree_->expand(rootIndex);
    showTree();
}

void ProjectTreePanel::clearRoot()
{
    tree_->setRootIndex(QModelIndex());
    showPlaceholder();
}

bool ProjectTreePanel::hasRoot() const
{
    return stack_->currentWidget() == tree_;
}

QString ProjectTreePanel::rootPath() const
{
    return model_->rootPath();
}

void ProjectTreePanel::showPlaceholder()
{
    stack_->setCurrentWidget(placeholder_);
}

void ProjectTreePanel::showTree()
{
    stack_->setCurrentWidget(tree_);
}

} // namespace VexaraEditor
