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

bool DocumentWorkspace::openFileAt(const QString& absolutePath, int line, int column)
{
    if (!openFile(absolutePath)) {
        return false;
    }

    CodeEditorWidget* editor = activeEditor();
    if (!editor) {
        return false;
    }

    return moveCursorTo(editor, line, column);
}

bool DocumentWorkspace::applyReplacement(const QString& absolutePath,
                                         int line,
                                         int column,
                                         int replaceLength,
                                         const QString& newText)
{
    if (!openFileAt(absolutePath, line, column)) {
        return false;
    }

    CodeEditorWidget* editor = activeEditor();
    if (!editor || !editor->editor()) {
        return false;
    }

    QPlainTextEdit* surface = editor->editor();
    QTextCursor cursor = surface->textCursor();
    if (replaceLength > 0) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, replaceLength);
    }
    cursor.insertText(newText);
    surface->setTextCursor(cursor);
    editor->document()->setModified(true);
    refreshTabTitle(editor);
    return true;
}

bool DocumentWorkspace::reloadFromDisk(const QString& absolutePath)
{
    const QString normalized = QFileInfo(absolutePath).absoluteFilePath();
    if (!QFileInfo(normalized).exists()) {
        return false;
    }

    QFile file(normalized);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QString contents = QString::fromUtf8(file.readAll());
    CodeEditorWidget* editor = editorForPath(normalized);
    if (!editor) {
        return openFile(normalized);
    }

    editor->setPlainText(contents);
    editor->document()->setModified(false);
    refreshTabTitle(editor);
    return true;
}

CodeEditorWidget* DocumentWorkspace::editorForPath(const QString& absolutePath) const
{
    const int index = findTabForPath(absolutePath);
    if (index < 0) {
        return nullptr;
    }
    return qobject_cast<CodeEditorWidget*>(tabs_->widget(index));
}

bool DocumentWorkspace::moveCursorTo(CodeEditorWidget* editor, int line, int column) const
{
    if (!editor || !editor->editor()) {
        return false;
    }

    QPlainTextEdit* surface = editor->editor();
    QTextCursor cursor = surface->textCursor();
    cursor.movePosition(QTextCursor::Start);
    const int targetLine = qMax(1, line);
    for (int i = 1; i < targetLine; ++i) {
        cursor.movePosition(QTextCursor::Down);
    }
    const int targetColumn = qMax(1, column);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, targetColumn - 1);
    surface->setTextCursor(cursor);
    surface->setFocus();
    return true;
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

QString DocumentWorkspace::activeSelectedText() const
{
    const CodeEditorWidget* editor = activeEditor();
    if (!editor || !editor->editor()) {
        return QString();
    }
    const QString selected = editor->editor()->textCursor().selectedText();
    return selected.isEmpty() ? QString() : selected;
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

bool DocumentWorkspace::replaceInActiveDocument(const QString& text,
                                                const QString& replacement,
                                                bool replaceAll)
{
    CodeEditorWidget* editor = activeEditor();
    if (!editor || text.isEmpty()) {
        return false;
    }

    QPlainTextEdit* surface = editor->editor();

    if (replaceAll) {
        QTextCursor cursor = surface->textCursor();
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::Start);
        surface->setTextCursor(cursor);

        int count = 0;
        while (editor->find(text)) {
            QTextCursor match = surface->textCursor();
            match.insertText(replacement);
            surface->setTextCursor(match);
            ++count;
        }
        cursor.endEditBlock();
        return count > 0;
    }

    QTextCursor cursor = surface->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == text) {
        cursor.insertText(replacement);
        surface->setTextCursor(cursor);
        editor->find(text);
        return true;
    }

    if (!editor->find(text)) {
        return false;
    }

    QTextCursor match = surface->textCursor();
    match.insertText(replacement);
    surface->setTextCursor(match);
    return true;
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
