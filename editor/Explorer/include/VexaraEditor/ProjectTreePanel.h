#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
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

signals:
    void fileActivated(const QString& absolutePath);

private:
    void showPlaceholder();
    void showTree();

    QFileSystemModel* model_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* placeholder_ = nullptr;
    QTreeView* tree_ = nullptr;
};

} // namespace VexaraEditor
