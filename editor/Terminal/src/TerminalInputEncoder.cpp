#include "VexaraEditor/TerminalInputEncoder.h"

#include <QKeyEvent>

namespace VexaraEditor {

bool TerminalInputEncoder::isCopyShortcut(const QKeyEvent& event)
{
    return event.matches(QKeySequence::Copy) && (event.modifiers() & Qt::ShiftModifier);
}

bool TerminalInputEncoder::isPasteShortcut(const QKeyEvent& event)
{
    return event.matches(QKeySequence::Paste)
           || (event.key() == Qt::Key_Insert && (event.modifiers() & Qt::ShiftModifier));
}

bool TerminalInputEncoder::isViewScrollKey(const QKeyEvent& event)
{
    if (event.modifiers() & Qt::ControlModifier) {
        switch (event.key()) {
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Home:
        case Qt::Key_End:
            return true;
        default:
            break;
        }
    }
    return false;
}

QByteArray TerminalInputEncoder::encode(const QKeyEvent& event)
{
    if (isCopyShortcut(event) || isPasteShortcut(event)) {
        return {};
    }

    const Qt::KeyboardModifiers mods = event.modifiers();

    if (mods & Qt::ControlModifier) {
        switch (event.key()) {
        case Qt::Key_C:
            return QByteArray("\x03");
        case Qt::Key_D:
            return QByteArray("\x04");
        case Qt::Key_Z:
            return QByteArray("\x1A");
        case Qt::Key_L:
            return QByteArray("\x0C");
        default:
            break;
        }
        if (!event.text().isEmpty()) {
            const QChar ch = event.text().at(0).toLower();
            if (ch.unicode() >= 1 && ch.unicode() <= 26) {
                return QByteArray(1, static_cast<char>(ch.unicode()));
            }
        }
    }

    switch (event.key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QByteArray("\r");
    case Qt::Key_Backspace:
        return QByteArray("\x7F");
    case Qt::Key_Tab:
        return QByteArray("\t");
    case Qt::Key_Escape:
        return QByteArray("\x1B");
    case Qt::Key_Up:
        return QByteArray("\x1B[A");
    case Qt::Key_Down:
        return QByteArray("\x1B[B");
    case Qt::Key_Right:
        return QByteArray("\x1B[C");
    case Qt::Key_Left:
        return QByteArray("\x1B[D");
    case Qt::Key_Home:
        return (mods & Qt::ShiftModifier) ? QByteArray("\x1B[1;2H") : QByteArray("\x1B[H");
    case Qt::Key_End:
        return (mods & Qt::ShiftModifier) ? QByteArray("\x1B[1;2F") : QByteArray("\x1B[F");
    case Qt::Key_PageUp:
        return QByteArray("\x1B[5~");
    case Qt::Key_PageDown:
        return QByteArray("\x1B[6~");
    case Qt::Key_Delete:
        return QByteArray("\x1B[3~");
    case Qt::Key_Insert:
        return QByteArray("\x1B[2~");
    default:
        break;
    }

    const QString text = event.text();
    if (!text.isEmpty()) {
        const QChar ch = text.at(0);
        if (ch.isPrint() || ch == QLatin1Char('\t')) {
            return text.toUtf8();
        }
    }

    return {};
}

} // namespace VexaraEditor
