#include "VexaraEditor/TerminalCaretOverlay.h"

#include "VexaraEditor/PlainTextTerminalRenderer.h"
#include "VexaraEditor/TerminalScreen.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTimer>

namespace VexaraEditor {

TerminalCaretOverlay::TerminalCaretOverlay(QPlainTextEdit* surface, QWidget* parent)
    : QWidget(parent)
    , surface_(surface)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);

    blinkTimer_ = new QTimer(this);
    connect(blinkTimer_, &QTimer::timeout, this, [this]() {
        caretVisible_ = !caretVisible_;
        update();
    });
    blinkTimer_->start(530);
}

void TerminalCaretOverlay::syncToDocumentPosition(int position)
{
    if (!surface_ || !parentWidget()) {
        return;
    }

    const int clamped = qBound(0, position, surface_->document()->characterCount());

    QTextCursor textCursor(surface_->document());
    textCursor.setPosition(clamped);
    surface_->document()->documentLayout()->update();

    // cursorRect() is in viewport coordinates; this widget's parent is the viewport.
    QRect caret = surface_->cursorRect(textCursor);
    if (caret.isNull()) {
        hide();
        return;
    }

    const QFontMetrics metrics(surface_->fontMetrics());
    if (caret.height() <= 0) {
        caret.setHeight(metrics.height());
    }

    const int width = qMax(2, metrics.horizontalAdvance(QLatin1Char('M')) / 6 + 2);
    const int height = qMax(caret.height(), metrics.height());
    setGeometry(caret.x(), caret.y(), width, height);
    show();
    raise();
    update();
}

void TerminalCaretOverlay::syncToScreen(const TerminalScreen& screen)
{
    syncToDocumentPosition(PlainTextTerminalRenderer::documentPositionForScreen(screen));
}

void TerminalCaretOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!caretVisible_) {
        return;
    }

    QPainter painter(this);
    painter.fillRect(rect(), QColor(240, 240, 240));
}

} // namespace VexaraEditor
