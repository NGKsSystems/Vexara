#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QStackedWidget;
class QTreeView;

namespace VexaraEditor {

class ProjectTreePanel : public QWidget {
    Q_OBJECT

public:
    explicit ProjectTreePanel(QWidget* parent = nullptr);

    void setRootPath(const QString& path);
    void clearRoot();
    bool hasRoot() const;
    QString rootPath() const;

    void showFileFilter();
    void hideFileFilter();
    bool isFileFilterVisible() const;

signals:
    void fileActivated(const QString& absolutePath);

private:
    void showPlaceholder();
    void showTree();
    void applyFileFilter(const QString& text);

    QFileSystemModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QWidget* filterBar_ = nullptr;
    QLineEdit* filterField_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* placeholder_ = nullptr;
    QTreeView* tree_ = nullptr;
};

} // namespace VexaraEditor
