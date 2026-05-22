#include "VexaraEditor/DocumentWorkspace.h"

#include "VexaraEditor/CodeEditorWidget.h"
#include "VexaraEditor/CodeHighlighter.h"
#include "VexaraEditor/LanguageRegistry.h"

#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextDocument>
#include <QVBoxLayout>

namespace VexaraEditor {

DocumentWorkspace::DocumentWorkspace(QWidget* parent)
    : QWidget(parent)
{
    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setDocumentMode(true);
    tabs_->setMovable(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs_);

    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget* page = tabs_->widget(index);
        tabs_->removeTab(index);
        delete page;
    });
}

bool DocumentWorkspace::openFile(const QString& absolutePath)
{
    const QFileInfo info(absolutePath);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    const int existing = findTabForPath(absolutePath);
    if (existing >= 0) {
        tabs_->setCurrentIndex(existing);
        return true;
    }

    if (isTabLimitReached()) {
        return false;
    }

    return addTabForFile(absolutePath);
}

int DocumentWorkspace::openDocumentCount() const
{
    return tabs_->count();
}

int DocumentWorkspace::maxOpenTabs() const
{
    return kMaxOpenTabs;
}

bool DocumentWorkspace::isTabLimitReached() const
{
    return tabs_->count() >= kMaxOpenTabs;
}

bool DocumentWorkspace::saveActiveFile()
{
    CodeEditorWidget* editor = activeEditor();
    if (!editor) {
        return false;
    }

    const QString path = editor->property("vexara_path").toString();
    if (path.isEmpty()) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    file.write(editor->toPlainText().toUtf8());
    editor->document()->setModified(false);
    refreshTabTitle(editor);
    return true;
}

int DocumentWorkspace::saveAllOpenFiles()
{
    int saved = 0;
    for (int i = 0; i < tabs_->count(); ++i) {
        const int previousIndex = tabs_->currentIndex();
        tabs_->setCurrentIndex(i);
        if (saveActiveFile()) {
            ++saved;
        }
        tabs_->setCurrentIndex(previousIndex);
    }
    return saved;
}

QString DocumentWorkspace::activeFilePath() const
{
    const CodeEditorWidget* editor = activeEditor();
    if (!editor) {
        return QString();
    }
    return editor->property("vexara_path").toString();
}

bool DocumentWorkspace::hasActiveUnsavedChanges() const
{
    const CodeEditorWidget* editor = activeEditor();
    return editor && editor->document()->isModified();
}

bool DocumentWorkspace::findInActiveDocument(const QString& text, bool forward)
{
    CodeEditorWidget* editor = activeEditor();
    if (!editor || text.isEmpty()) {
        return false;
    }

    QTextDocument::FindFlags flags;
    if (!forward) {
        flags |= QTextDocument::FindBackward;
    }
    return editor->find(text, flags);
}

CodeEditorWidget* DocumentWorkspace::activeEditor() const
{
    return qobject_cast<CodeEditorWidget*>(tabs_->currentWidget());
}

int DocumentWorkspace::findTabForPath(const QString& absolutePath) const
{
    const QString normalized = QFileInfo(absolutePath).absoluteFilePath();
    for (int i = 0; i < tabs_->count(); ++i) {
        QWidget* page = tabs_->widget(i);
        if (page && page->property("vexara_path").toString() == normalized) {
            return i;
        }
    }
    return -1;
}

bool DocumentWorkspace::addTabForFile(const QString& absolutePath)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    auto* editor = new CodeEditorWidget(this);
    editor->setPlainText(QString::fromUtf8(file.readAll()));
    editor->setProperty("vexara_path", QFileInfo(absolutePath).absoluteFilePath());
    editor->document()->setModified(false);
    bindEditor(editor);

    auto* highlighter = new CodeHighlighter(editor->document());
    highlighter->setLanguage(LanguageRegistry::languageForPath(absolutePath));

    const QString title = QFileInfo(absolutePath).fileName();
    tabs_->addTab(editor, title);
    tabs_->setCurrentWidget(editor);
    return true;
}

void DocumentWorkspace::bindEditor(CodeEditorWidget* editor)
{
    connect(editor->editor(), &QPlainTextEdit::textChanged, this, [this, editor]() {
        refreshTabTitle(editor);
    });
}

void DocumentWorkspace::refreshTabTitle(CodeEditorWidget* editor)
{
    const int index = tabs_->indexOf(editor);
    if (index < 0) {
        return;
    }

    const QString path = editor->property("vexara_path").toString();
    QString title = QFileInfo(path).fileName();
    if (title.isEmpty()) {
        title = QStringLiteral("Untitled");
    }
    if (editor->document()->isModified()) {
        title += QStringLiteral(" *");
    }
    tabs_->setTabText(index, title);
}

} // namespace VexaraEditor
