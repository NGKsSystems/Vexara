#include "VexaraEditor/ProjectTreePanel.h"

#include "VexaraEditor/TextContextMenu.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>

namespace VexaraEditor {

namespace {

void hideExtraColumns(QTreeView* tree, int columnCount)
{
    for (int column = 1; column < columnCount; ++column) {
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

    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(0);
    proxy_->setRecursiveFilteringEnabled(true);

    filterBar_ = new QWidget(this);
    auto* filterLayout = new QVBoxLayout(filterBar_);
    filterLayout->setContentsMargins(6, 6, 6, 0);
    filterField_ = new QLineEdit(filterBar_);
    filterField_->setPlaceholderText(QStringLiteral("Filter files"));
    filterField_->setClearButtonEnabled(true);
    filterLayout->addWidget(filterField_);
    filterBar_->setVisible(false);
    installLineEditContextMenu(filterField_);

    placeholder_ = new QLabel(
        QStringLiteral("No folder opened.\n\nUse File > Open Folder\nor the Open menu on the toolbar."),
        this);
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);

    tree_ = new QTreeView(this);
    tree_->setModel(proxy_);
    tree_->setAnimated(false);
    tree_->setIndentation(18);
    tree_->setTextElideMode(Qt::ElideNone);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tree_->setUniformRowHeights(true);
    tree_->setWordWrap(false);
    tree_->setMinimumWidth(240);

    hideExtraColumns(tree_, proxy_->columnCount());
    tuneNameColumn(tree_);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(placeholder_);
    stack_->addWidget(tree_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(filterBar_);
    layout->addWidget(stack_, 1);

    connect(filterField_, &QLineEdit::textChanged, this, &ProjectTreePanel::applyFileFilter);

    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        const QFileInfo info(model_->filePath(proxy_->mapToSource(index)));
        if (info.isFile()) {
            emit fileActivated(info.absoluteFilePath());
        }
    });

    connect(tree_, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        tree_->setToolTip(model_->filePath(proxy_->mapToSource(index)));
    });

    connect(tree_, &QTreeView::entered, this, [this](const QModelIndex& index) {
        tree_->setToolTip(model_->filePath(proxy_->mapToSource(index)));
    });

    showPlaceholder();
}

void ProjectTreePanel::setRootPath(const QString& path)
{
    const QModelIndex rootIndex = model_->setRootPath(path);
    tree_->setRootIndex(proxy_->mapFromSource(rootIndex));
    hideExtraColumns(tree_, proxy_->columnCount());
    tuneNameColumn(tree_);
    tree_->expand(proxy_->mapFromSource(rootIndex));
    showTree();
}

void ProjectTreePanel::clearRoot()
{
    filterField_->clear();
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

void ProjectTreePanel::showFileFilter()
{
    filterBar_->setVisible(true);
    filterField_->setFocus();
    filterField_->selectAll();
}

void ProjectTreePanel::hideFileFilter()
{
    filterBar_->setVisible(false);
    filterField_->clear();
    applyFileFilter(QString());
}

bool ProjectTreePanel::isFileFilterVisible() const
{
    return filterBar_->isVisible();
}

void ProjectTreePanel::showPlaceholder()
{
    stack_->setCurrentWidget(placeholder_);
}

void ProjectTreePanel::showTree()
{
    stack_->setCurrentWidget(tree_);
}

void ProjectTreePanel::applyFileFilter(const QString& text)
{
    proxy_->setFilterFixedString(text.trimmed());
    if (!text.trimmed().isEmpty() && hasRoot()) {
        tree_->expandAll();
    }
}

} // namespace VexaraEditor
