#pragma once

#include <functional>

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPoint>
#include <QTextEdit>
#include <QWidget>

namespace VexaraEditor {

inline bool isClipboardShortcut(const QKeyEvent* event)
{
    if (!event) {
        return false;
    }
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_C:
        case Qt::Key_V:
        case Qt::Key_X:
        case Qt::Key_A:
        case Qt::Key_Insert:
            return true;
        default:
            break;
        }
    }
    if ((mods & Qt::ShiftModifier) && event->key() == Qt::Key_Insert) {
        return true;
    }
    return false;
}

inline bool dispatchCopyToFocusWidget()
{
    QWidget* focused = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QPlainTextEdit*>(focused)) {
        edit->copy();
        return true;
    }
    if (auto* edit = qobject_cast<QTextEdit*>(focused)) {
        edit->copy();
        return true;
    }
    if (auto* field = qobject_cast<QLineEdit*>(focused)) {
        field->copy();
        return true;
    }
    return false;
}

inline bool dispatchCutToFocusWidget()
{
    QWidget* focused = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QPlainTextEdit*>(focused)) {
        edit->cut();
        return true;
    }
    if (auto* edit = qobject_cast<QTextEdit*>(focused)) {
        edit->cut();
        return true;
    }
    if (auto* field = qobject_cast<QLineEdit*>(focused)) {
        field->cut();
        return true;
    }
    return false;
}

inline bool dispatchPasteToFocusWidget()
{
    QWidget* focused = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QPlainTextEdit*>(focused)) {
        edit->paste();
        return true;
    }
    if (auto* edit = qobject_cast<QTextEdit*>(focused)) {
        edit->paste();
        return true;
    }
    if (auto* field = qobject_cast<QLineEdit*>(focused)) {
        field->paste();
        return true;
    }
    return false;
}

inline bool dispatchSelectAllToFocusWidget()
{
    QWidget* focused = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QPlainTextEdit*>(focused)) {
        edit->selectAll();
        return true;
    }
    if (auto* edit = qobject_cast<QTextEdit*>(focused)) {
        edit->selectAll();
        return true;
    }
    if (auto* field = qobject_cast<QLineEdit*>(focused)) {
        field->selectAll();
        return true;
    }
    return false;
}

inline void installLineEditContextMenu(QLineEdit* field, bool readOnly = false)
{
    if (!field) {
        return;
    }

    field->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(field, &QWidget::customContextMenuRequested, field,
                     [field, readOnly](const QPoint& pos) {
                         QMenu menu;
                         QAction* cut = menu.addAction(QStringLiteral("Cut"));
                         QAction* copy = menu.addAction(QStringLiteral("Copy"));
                         QAction* paste = menu.addAction(QStringLiteral("Paste"));
                         QAction* selectAll = menu.addAction(QStringLiteral("Select All"));

                         cut->setShortcut(QKeySequence::Cut);
                         copy->setShortcut(QKeySequence::Copy);
                         paste->setShortcut(QKeySequence::Paste);
                         selectAll->setShortcut(QKeySequence::SelectAll);

                         cut->setEnabled(!readOnly && field->hasSelectedText());
                         copy->setEnabled(field->hasSelectedText());
                         paste->setEnabled(!readOnly
                                           && !QGuiApplication::clipboard()->text().isEmpty());
                         selectAll->setEnabled(!field->text().isEmpty());

                         QObject::connect(cut, &QAction::triggered, field, &QLineEdit::cut);
                         QObject::connect(copy, &QAction::triggered, field, &QLineEdit::copy);
                         QObject::connect(paste, &QAction::triggered, field, &QLineEdit::paste);
                         QObject::connect(selectAll, &QAction::triggered, field, &QLineEdit::selectAll);

                         menu.exec(field->mapToGlobal(pos));
                     });
}

inline void installPlainTextContextMenu(
    QPlainTextEdit* edit,
    bool readOnly = false,
    const std::function<void(const QString&)>& customPaste = {})
{
    if (!edit) {
        return;
    }

    edit->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(edit, &QWidget::customContextMenuRequested, edit,
                     [edit, readOnly, customPaste](const QPoint& pos) {
                         QMenu menu;
                         QAction* cut = menu.addAction(QStringLiteral("Cut"));
                         QAction* copy = menu.addAction(QStringLiteral("Copy"));
                         QAction* paste = menu.addAction(QStringLiteral("Paste"));
                         QAction* selectAll = menu.addAction(QStringLiteral("Select All"));

                         cut->setShortcut(QKeySequence::Cut);
                         copy->setShortcut(QKeySequence::Copy);
                         paste->setShortcut(QKeySequence::Paste);
                         selectAll->setShortcut(QKeySequence::SelectAll);

                         const bool hasSelection = edit->textCursor().hasSelection();
                         const bool hasClipboard =
                             !QGuiApplication::clipboard()->text().isEmpty();
                         const bool useCustomPaste = static_cast<bool>(customPaste);

                         cut->setEnabled(!readOnly && hasSelection);
                         copy->setEnabled(hasSelection);
                         paste->setEnabled((!readOnly || useCustomPaste) && hasClipboard);
                         selectAll->setEnabled(!edit->document()->isEmpty());

                         QObject::connect(cut, &QAction::triggered, edit, &QPlainTextEdit::cut);
                         QObject::connect(copy, &QAction::triggered, edit, &QPlainTextEdit::copy);
                         if (useCustomPaste) {
                             QObject::connect(paste, &QAction::triggered, edit, [customPaste]() {
                                 customPaste(QGuiApplication::clipboard()->text());
                             });
                         } else {
                             QObject::connect(paste, &QAction::triggered, edit, &QPlainTextEdit::paste);
                         }
                         QObject::connect(selectAll, &QAction::triggered, edit, &QPlainTextEdit::selectAll);

                         menu.exec(edit->mapToGlobal(pos));
                     });
}

} // namespace VexaraEditor
