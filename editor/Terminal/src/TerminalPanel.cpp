#include "VexaraEditor/TerminalPanel.h"

#include "VexaraEditor/TerminalCaretOverlay.h"
#include "VexaraEditor/AnsiStrip.h"
#include "VexaraEditor/PlainTextTerminalRenderer.h"
#include "VexaraEditor/TerminalInputEncoder.h"
#include "VexaraEditor/TerminalSession.h"
#include "VexaraEditor/TerminalScreen.h"
#include "VexaraEditor/TerminalCell.h"
#include "VexaraEditor/TextContextMenu.h"
#include "VexaraEditor/WinConPty.h"

#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QPalette>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QShortcutEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextCursor>
#include <QVBoxLayout>

namespace VexaraEditor {

TerminalPanel::TerminalPanel(QWidget* parent)
    : QWidget(parent)
    , session_(new TerminalSession(this))
    , renderer_(new PlainTextTerminalRenderer())
    , lineBridgeProcess_(new QProcess(this))
    , resizeDebounce_(new QTimer(this))
{
    surface_ = new QPlainTextEdit(this);
    surface_->setFocusPolicy(Qt::StrongFocus);
    surface_->setLineWrapMode(QPlainTextEdit::NoWrap);
    surface_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    surface_->setUndoRedoEnabled(false);
    surface_->setReadOnly(true);
    surface_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    surface_->setCursorWidth(0);
    QFont mono = surface_->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily(QStringLiteral("Consolas"));
    surface_->setFont(mono);
    QPalette palette = surface_->palette();
    palette.setColor(QPalette::Base, QColor(24, 24, 24));
    palette.setColor(QPalette::Text, QColor(220, 220, 220));
    surface_->setPalette(palette);
    surface_->setStyleSheet(QStringLiteral("QPlainTextEdit { selection-background-color: #264f78; }"));
    surface_->installEventFilter(this);
    surface_->viewport()->installEventFilter(this);

    caretOverlay_ = new TerminalCaretOverlay(surface_, surface_->viewport());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(surface_);

    connect(surface_, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        if (backend_ == Backend::ConPty && session_->isActive() && !mouseSelecting_) {
            lockCaretToScreen();
        }
    });
    connect(surface_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        QScrollBar* vbar = surface_->verticalScrollBar();
        scrollPinnedToBottom_ = vbar->value() >= vbar->maximum() - 2;
        if (backend_ == Backend::ConPty && session_->isActive() && caretOverlay_) {
            caretOverlay_->syncToScreen(session_->screen());
        }
    });
    connect(surface_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (backend_ == Backend::ConPty && session_->isActive()) {
            lockCaretToScreen();
        }
    });

    resizeDebounce_->setSingleShot(true);
    resizeDebounce_->setInterval(100);
    connect(resizeDebounce_, &QTimer::timeout, this, &TerminalPanel::applyTerminalSize);

    connect(session_, &TerminalSession::screenUpdated, this, &TerminalPanel::refreshView);
    connect(session_, &TerminalSession::sessionEnded, this, [this](const QString& message) {
        if (restartingShell_) {
            return;
        }
        stopBackend();
        if (!message.isEmpty()) {
            surface_->appendPlainText(message.endsWith(QLatin1Char('\n')) ? message : message + QLatin1Char('\n'));
        }
        surface_->setReadOnly(true);
        if (caretOverlay_) {
            caretOverlay_->hide();
        }
    });

    connect(lineBridgeProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        QString chunk = decodeTerminalBytes(lineBridgeProcess_->readAllStandardOutput());
        while (chunk.endsWith(lineBridgePrompt_)) {
            chunk.chop(lineBridgePrompt_.size());
        }
        if (!chunk.isEmpty()) {
            appendLineBridgeOutput(chunk);
        }
    });

    surface_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(surface_, &QWidget::customContextMenuRequested, this, &TerminalPanel::showSurfaceContextMenu);
}

void TerminalPanel::setProfile(const VexaraCore::TerminalProfile& profile)
{
    profile_ = profile;
    emit profileChanged(profile_.id);
}

VexaraCore::TerminalProfile TerminalPanel::profile() const
{
    return profile_;
}

void TerminalPanel::setWorkingDirectory(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        return;
    }
    workingDirectory_ = info.absoluteFilePath();
    restartShell();
}

QString TerminalPanel::workingDirectory() const
{
    return workingDirectory_;
}

namespace {

QString normalizeTerminalPaste(const QString& text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.remove(QLatin1Char('\r'));
    return normalized;
}

} // namespace

void TerminalPanel::pasteFromClipboard()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        return;
    }
    if (backend_ == Backend::ConPty && session_->isActive()) {
        const QString normalized = normalizeTerminalPaste(text);
        session_->writeInput(normalized.toUtf8());
        return;
    }
    if (backend_ == Backend::LineBridge && !surface_->isReadOnly()) {
        surface_->paste();
    }
}

bool TerminalPanel::containsFocusWidget(const QWidget* widget) const
{
    return widget != nullptr && (widget == surface_ || isAncestorOf(widget));
}

void TerminalPanel::focusCommandLine()
{
    surface_->setFocus(Qt::OtherFocusReason);
    updateCaretOverlay();
}

void TerminalPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    scheduleResize();
}

void TerminalPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    scheduleResize();
    focusCommandLine();
}

bool TerminalPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == surface_ && backend_ == Backend::ConPty && session_->isActive()) {
        if (event->type() == QEvent::MouseButtonPress) {
            mouseSelecting_ = false;
        } else if (event->type() == QEvent::MouseMove) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                mouseSelecting_ = true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (!mouseSelecting_ && !surface_->textCursor().hasSelection()) {
                lockCaretToScreen();
            }
            mouseSelecting_ = false;
        }
    }

    if (watched == surface_ && event->type() == QEvent::Shortcut) {
        auto* shortcutEvent = static_cast<QShortcutEvent*>(event);
        if (backend_ == Backend::ConPty && session_->isActive()
            && shortcutEvent->key().matches(QKeySequence::Paste)) {
            pasteFromClipboard();
            return true;
        }
    }

    if ((watched == surface_ || watched == surface_->viewport()) && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (backend_ == Backend::ConPty && session_->isActive()) {
            if (TerminalInputEncoder::isViewScrollKey(*keyEvent)) {
                return false;
            }
            if (TerminalInputEncoder::isCopyShortcut(*keyEvent)) {
                surface_->copy();
                return true;
            }
            if (TerminalInputEncoder::isPasteShortcut(*keyEvent)) {
                pasteFromClipboard();
                return true;
            }
            const QByteArray encoded = TerminalInputEncoder::encode(*keyEvent);
            if (!encoded.isEmpty()) {
                session_->writeInput(encoded);
                return true;
            }
            return false;
        }
        if (backend_ == Backend::LineBridge) {
            return handleLineBridgeKey(keyEvent);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TerminalPanel::stopBackend()
{
    session_->stop();
    if (lineBridgeProcess_->state() != QProcess::NotRunning) {
        lineBridgeProcess_->kill();
        lineBridgeProcess_->waitForFinished(300);
    }
    backend_ = Backend::None;
}

void TerminalPanel::restartShell()
{
    restartingShell_ = true;
    stopBackend();
    if (caretOverlay_) {
        caretOverlay_->hide();
    }
    surface_->clear();
    lineBridgeReadOnlyEnd_ = 0;
    lineBridgePrompt_.clear();

    if (workingDirectory_.isEmpty()) {
        surface_->setPlainText(QStringLiteral("Open a folder to activate the terminal.\n"));
        surface_->setReadOnly(true);
        restartingShell_ = false;
        return;
    }

    if (profile_.program.isEmpty()) {
        surface_->setPlainText(QStringLiteral("No terminal profile selected.\n"));
        surface_->setReadOnly(true);
        restartingShell_ = false;
        return;
    }

    if (!QFileInfo::exists(profile_.program)) {
        surface_->setPlainText(
            QStringLiteral("Shell not found: %1\nCheck Settings or install the shell, then pick it from the Shell menu.\n")
                .arg(profile_.program));
        surface_->setReadOnly(true);
        restartingShell_ = false;
        return;
    }

    scrollPinnedToBottom_ = true;

    if (WinConPty::isAvailable() && startConPty()) {
        restartingShell_ = false;
        return;
    }

    surface_->appendPlainText(
        QStringLiteral("Failed to start %1. Check that the executable exists and try again.\n")
            .arg(profile_.displayName));
    surface_->setReadOnly(true);
    restartingShell_ = false;
}

bool TerminalPanel::startConPty()
{
    applyTerminalSize();
    if (!session_->startConPty(profile_, workingDirectory_, terminalColumns(), terminalRows())) {
        return false;
    }
    backend_ = Backend::ConPty;
    surface_->setReadOnly(true);
    refreshView();
    focusCommandLine();
    updateCaretOverlay();
    return true;
}

bool TerminalPanel::startLineBridge()
{
    backend_ = Backend::LineBridge;
    surface_->setReadOnly(false);
    lineBridgePrompt_ = QDir::toNativeSeparators(workingDirectory_) + QStringLiteral("> ");
    appendLineBridgeOutput(QStringLiteral("Vexara terminal [")
                           + profile_.displayName
                           + QStringLiteral("] - line bridge (ConPTY unavailable).\n"));
    appendLineBridgePrompt();

    lineBridgeProcess_->setProcessChannelMode(QProcess::MergedChannels);
    lineBridgeProcess_->setProgram(profile_.program);
    lineBridgeProcess_->setArguments(profile_.args);
    lineBridgeProcess_->setWorkingDirectory(workingDirectory_);
    lineBridgeProcess_->start();

    if (lineBridgeProcess_->state() != QProcess::Running) {
        appendLineBridgeOutput(QStringLiteral("Failed to start: ") + profile_.program + QStringLiteral("\n"));
        appendLineBridgePrompt();
    }

    focusCommandLine();
    updateCaretOverlay();
    return true;
}

void TerminalPanel::refreshView()
{
    if (backend_ != Backend::ConPty || !session_->isActive()) {
        caretOverlay_->hide();
        return;
    }

    renderer_->apply(session_->screen(), surface_, scrollPinnedToBottom_);
    lockCaretToScreen();
}

void TerminalPanel::lockCaretToScreen()
{
    if (backend_ != Backend::ConPty || !session_->isActive()) {
        return;
    }
    const TerminalScreen& screen = session_->screen();
    const auto& rows = screen.cells();
    bool hasContent = false;
    for (const auto& line : rows) {
        for (const TerminalCell& cell : line) {
            if (cell.ch != QLatin1Char(' ')) {
                hasContent = true;
                break;
            }
        }
        if (hasContent) {
            break;
        }
    }
    if (!hasContent) {
        if (caretOverlay_) {
            caretOverlay_->hide();
        }
        return;
    }
    if (scrollPinnedToBottom_ && screen.psReadLineCompat()) {
        renderer_->placeCaret(screen, surface_, true);
    } else if (caretOverlay_) {
        caretOverlay_->syncToScreen(screen);
    }
}

void TerminalPanel::updateCaretOverlay()
{
    if (!caretOverlay_) {
        return;
    }
    if (backend_ == Backend::ConPty && session_->isActive()) {
        lockCaretToScreen();
        return;
    }
    if (backend_ == Backend::LineBridge && !lineBridgePrompt_.isEmpty()) {
        const int promptPos = lineBridgePromptPosition() + lineBridgePrompt_.size();
        caretOverlay_->syncToDocumentPosition(promptPos);
        return;
    }
    caretOverlay_->hide();
}

void TerminalPanel::scheduleResize()
{
    if (backend_ == Backend::ConPty) {
        resizeDebounce_->start();
    }
}

void TerminalPanel::applyTerminalSize()
{
    if (backend_ != Backend::ConPty || !session_->isActive()) {
        return;
    }
    session_->resize(terminalColumns(), terminalRows());
}

void TerminalPanel::appendLineBridgeOutput(const QString& text)
{
    if (text.isEmpty()) {
        return;
    }
    QTextCursor cursor(surface_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (!text.endsWith(QLatin1Char('\n'))) {
        cursor.insertText(QStringLiteral("\n"));
    }
    lineBridgeReadOnlyEnd_ = cursor.position();
}

void TerminalPanel::appendLineBridgePrompt()
{
    QTextCursor cursor(surface_->document());
    cursor.movePosition(QTextCursor::End);
    if (cursor.position() > lineBridgeReadOnlyEnd_) {
        cursor.setPosition(lineBridgeReadOnlyEnd_);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(lineBridgePrompt_);
    lineBridgeReadOnlyEnd_ = cursor.position();
    cursor.movePosition(QTextCursor::End);
    surface_->setTextCursor(cursor);
}

void TerminalPanel::executeLineBridgeCommand()
{
    if (workingDirectory_.isEmpty() || lineBridgeProcess_->state() != QProcess::Running) {
        return;
    }

    const QString command = currentLineBridgeInput().trimmed();

    QTextCursor cursor(surface_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("\n"));
    lineBridgeReadOnlyEnd_ = cursor.position();

    if (command.isEmpty()) {
        appendLineBridgePrompt();
        return;
    }

    lineBridgeProcess_->write((command + QStringLiteral("\r\n")).toLocal8Bit());
    QTimer::singleShot(80, this, [this]() {
        appendLineBridgePrompt();
        focusCommandLine();
    });
}

void TerminalPanel::showSurfaceContextMenu(const QPoint& pos)
{
    QMenu menu;
    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    QAction* paste = menu.addAction(QStringLiteral("Paste"));
    QAction* selectAll = menu.addAction(QStringLiteral("Select All"));
    QAction* clear = menu.addAction(QStringLiteral("Clear"));

    const bool hasSelection = surface_->textCursor().hasSelection();
    const bool hasClipboard = !QGuiApplication::clipboard()->text().isEmpty();
    const bool running = (backend_ == Backend::ConPty && session_->isActive())
                         || (backend_ == Backend::LineBridge
                             && lineBridgeProcess_->state() == QProcess::Running);

    copy->setEnabled(hasSelection);
    paste->setEnabled(hasClipboard && running);
    selectAll->setEnabled(!surface_->document()->isEmpty());

    connect(copy, &QAction::triggered, surface_, &QPlainTextEdit::copy);
    connect(paste, &QAction::triggered, this, &TerminalPanel::pasteFromClipboard);
    connect(selectAll, &QAction::triggered, surface_, &QPlainTextEdit::selectAll);
    connect(clear, &QAction::triggered, this, [this]() {
        if (backend_ == Backend::ConPty) {
            restartShell();
        } else {
            surface_->clear();
            if (backend_ == Backend::LineBridge) {
                appendLineBridgePrompt();
            }
        }
    });

    menu.exec(surface_->mapToGlobal(pos));
}

bool TerminalPanel::handleLineBridgeKey(QKeyEvent* event)
{
    if (workingDirectory_.isEmpty() || surface_->isReadOnly()) {
        return false;
    }

    if (isClipboardShortcut(event)) {
        return false;
    }

    const int promptPos = lineBridgePromptPosition();
    QTextCursor cursor = surface_->textCursor();

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        executeLineBridgeCommand();
        return true;
    }

    if (event->key() == Qt::Key_Backspace) {
        if (cursor.position() <= promptPos) {
            return true;
        }
        if (cursor.hasSelection() && cursor.selectionStart() < promptPos) {
            cursor.setPosition(promptPos);
            surface_->setTextCursor(cursor);
            return true;
        }
        return false;
    }

    if (event->key() == Qt::Key_Delete) {
        if (cursor.position() < promptPos) {
            return true;
        }
        if (cursor.hasSelection() && cursor.selectionStart() < promptPos) {
            cursor.setPosition(promptPos);
            surface_->setTextCursor(cursor);
            return true;
        }
        return false;
    }

    if (event->key() == Qt::Key_Home && !event->modifiers().testFlag(Qt::ControlModifier)) {
        cursor.setPosition(promptPos);
        surface_->setTextCursor(cursor);
        return true;
    }

    if (event->key() == Qt::Key_Left && cursor.position() <= promptPos && !cursor.hasSelection()) {
        return true;
    }

    if (cursor.position() < promptPos) {
        cursor.setPosition(promptPos);
        surface_->setTextCursor(cursor);
    }

    return false;
}

QString TerminalPanel::currentLineBridgeInput() const
{
    const int promptPos = lineBridgePromptPosition();
    if (promptPos < 0) {
        return QString();
    }

    QTextCursor cursor(surface_->document());
    cursor.setPosition(promptPos + lineBridgePrompt_.size());
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return cursor.selectedText();
}

int TerminalPanel::lineBridgePromptPosition() const
{
    if (lineBridgePrompt_.isEmpty()) {
        return lineBridgeReadOnlyEnd_;
    }
    return qMax(0, surface_->toPlainText().lastIndexOf(lineBridgePrompt_));
}

int TerminalPanel::terminalColumns() const
{
    const int width = surface_->viewport()->width();
    const QFontMetrics metrics(surface_->font());
    const int charWidth = qMax(8, metrics.horizontalAdvance(QLatin1Char('M')));
    return qMax(40, width / charWidth);
}

int TerminalPanel::terminalRows() const
{
    const int height = surface_->viewport()->height();
    const QFontMetrics metrics(surface_->font());
    const int lineHeight = qMax(12, metrics.lineSpacing());
    return qMax(8, height / lineHeight);
}

} // namespace VexaraEditor
