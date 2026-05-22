#pragma once

#include <QWidget>

class QTabWidget;

namespace VexaraEditor {

class CodeEditorWidget;

class DocumentWorkspace : public QWidget {
public:
    static constexpr int kMaxOpenTabs = 12;

    explicit DocumentWorkspace(QWidget* parent = nullptr);

    bool openFile(const QString& absolutePath);
    int openDocumentCount() const;
    int maxOpenTabs() const;
    bool isTabLimitReached() const;

    bool saveActiveFile();
    int saveAllOpenFiles();
    QString activeFilePath() const;
    bool hasActiveUnsavedChanges() const;

    bool findInActiveDocument(const QString& text, bool forward);
    bool replaceInActiveDocument(const QString& text, const QString& replacement, bool replaceAll);

private:
    CodeEditorWidget* activeEditor() const;
    int findTabForPath(const QString& absolutePath) const;
    bool addTabForFile(const QString& absolutePath);
    void bindEditor(CodeEditorWidget* editor);
    void refreshTabTitle(CodeEditorWidget* editor);

    QTabWidget* tabs_ = nullptr;
};

} // namespace VexaraEditor
