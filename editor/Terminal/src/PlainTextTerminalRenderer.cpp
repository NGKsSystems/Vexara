#include "VexaraEditor/PlainTextTerminalRenderer.h"

#include "VexaraEditor/TerminalScreen.h"

#include <QColor>
#include <QFont>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>

namespace VexaraEditor {

namespace {

QColor ansiColor(quint8 index, bool bold)
{
    static const QColor palette[16] = {
        QColor(0, 0, 0),
        QColor(128, 0, 0),
        QColor(0, 128, 0),
        QColor(128, 128, 0),
        QColor(0, 0, 128),
        QColor(128, 0, 128),
        QColor(0, 128, 128),
        QColor(192, 192, 192),
        QColor(128, 128, 128),
        QColor(255, 0, 0),
        QColor(0, 255, 0),
        QColor(255, 255, 0),
        QColor(0, 0, 255),
        QColor(255, 0, 255),
        QColor(0, 255, 255),
        QColor(255, 255, 255),
    };
    const int idx = qBound(0, static_cast<int>(index), 15);
    QColor color = palette[idx];
    if (bold && idx < 8) {
        color = palette[idx + 8];
    }
    return color;
}

QTextCharFormat formatForCell(const TerminalCell& cell)
{
    QTextCharFormat format;
    format.setForeground(ansiColor(cell.fg, cell.bold));
    format.setBackground(ansiColor(cell.bg, false));
    if (cell.bold) {
        format.setFontWeight(QFont::Bold);
    }
    if (cell.underline) {
        format.setFontUnderline(true);
    }
    return format;
}

int trimmedLineLength(const QVector<TerminalCell>& line)
{
    int last = line.size() - 1;
    while (last >= 0 && line[last].ch == QLatin1Char(' ')) {
        --last;
    }
    return last + 1;
}

bool isRowBlank(const QVector<TerminalCell>& line)
{
    return trimmedLineLength(line) == 0;
}

int lastNonEmptyRow(const QVector<QVector<TerminalCell>>& rows)
{
    for (int row = rows.size() - 1; row >= 0; --row) {
        if (!isRowBlank(rows[row])) {
            return row;
        }
    }
    return -1;
}

int lineDisplayLength(const QVector<TerminalCell>& line, int row, int cursorRow, int cursorCol)
{
    int length = trimmedLineLength(line);
    if (row == cursorRow) {
        length = qMax(length, cursorCol + 1);
        length = qMin(length, line.size());
    }
    return length;
}

void appendLineToCursor(QTextCursor& cursor,
                          const QVector<TerminalCell>& line,
                          int row,
                          int cursorRow,
                          int cursorCol)
{
    const int length = lineDisplayLength(line, row, cursorRow, cursorCol);
    for (int col = 0; col < length; ++col) {
        cursor.insertText(QString(line[col].ch), formatForCell(line[col]));
    }
}

int scrollbackDocumentLength(const QVector<QVector<TerminalCell>>& scrollback)
{
    int position = 0;
    for (int i = 0; i < scrollback.size(); ++i) {
        position += trimmedLineLength(scrollback[i]);
        position += 1;
    }
    return position;
}

int documentOffsetForRowCol(const QVector<QVector<TerminalCell>>& rows,
                            int firstRow,
                            int row,
                            int col,
                            int cursorRow,
                            int cursorCol)
{
    int position = 0;
    for (int r = firstRow; r < row; ++r) {
        position += lineDisplayLength(rows[r], r, cursorRow, cursorCol);
        position += 1;
    }
    const int rowLength = lineDisplayLength(rows[row], row, cursorRow, cursorCol);
    position += qBound(0, col, rowLength);
    return position;
}

void caretAnchorForScreen(const TerminalScreen& screen,
                          const QVector<QVector<TerminalCell>>& rows,
                          int& row,
                          int& col)
{
    if (screen.psReadLineCompat()) {
        row = screen.caretRow();
        col = screen.caretColumn();
    } else {
        // cmd/Git Bash redraw the active line and lie about DEC cursor position; the caret
        // belongs at the end of the last non-empty line (where you are typing).
        row = lastNonEmptyRow(rows);
        col = row >= 0 ? trimmedLineLength(rows[row]) : 0;
    }
    if (rows.isEmpty()) {
        row = 0;
        col = 0;
        return;
    }
    row = qBound(0, row, rows.size() - 1);
    col = qBound(0, col, rows[row].size());
}

} // namespace

int PlainTextTerminalRenderer::documentPositionForScreen(const TerminalScreen& screen)
{
    const auto& scrollback = screen.scrollback();
    const auto& rows = screen.cells();
    if (rows.isEmpty()) {
        return scrollbackDocumentLength(scrollback);
    }

    int anchorRow = 0;
    int anchorCol = 0;
    caretAnchorForScreen(screen, rows, anchorRow, anchorCol);
    return scrollbackDocumentLength(scrollback)
           + documentOffsetForRowCol(rows, 0, anchorRow, anchorCol, anchorRow, anchorCol);
}

void PlainTextTerminalRenderer::apply(const TerminalScreen& screen, QPlainTextEdit* view, bool scrollToBottom)
{
    if (!view) {
        return;
    }

    QScrollBar* vbar = view->verticalScrollBar();
    const bool atBottom = vbar->value() >= vbar->maximum() - 2;
    const int savedScrollValue = vbar->value();
    const int savedScrollMaximum = vbar->maximum();

    QTextCursor cursor(view->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();

    const auto& scrollback = screen.scrollback();
    const auto& rows = screen.cells();
    int cursorRow = 0;
    int cursorCol = 0;
    caretAnchorForScreen(screen, rows, cursorRow, cursorCol);

    for (int i = 0; i < scrollback.size(); ++i) {
        appendLineToCursor(cursor, scrollback[i], -1, cursorRow, cursorCol);
        cursor.insertText(QStringLiteral("\n"));
    }

    for (int row = 0; row < rows.size(); ++row) {
        appendLineToCursor(cursor, rows[row], row, cursorRow, cursorCol);
        if (row + 1 < rows.size()) {
            cursor.insertText(QStringLiteral("\n"));
        }
    }
    cursor.endEditBlock();

    if (scrollToBottom && atBottom) {
        vbar->setValue(vbar->maximum());
    } else if (!scrollToBottom && savedScrollMaximum > 0) {
        const int newMaximum = vbar->maximum();
        const int restored = (savedScrollValue * newMaximum) / savedScrollMaximum;
        vbar->setValue(qBound(0, restored, newMaximum));
    } else if (!scrollToBottom) {
        vbar->setValue(qBound(0, savedScrollValue, vbar->maximum()));
    }
}

void PlainTextTerminalRenderer::placeCaret(const TerminalScreen& screen,
                                             QPlainTextEdit* view,
                                             bool ensureVisible)
{
    if (!view) {
        return;
    }

    const int position = PlainTextTerminalRenderer::documentPositionForScreen(screen);
    const int clamped = qBound(0, position, view->document()->characterCount());

    QTextCursor cursor(view->document());
    cursor.setPosition(clamped);
    view->setTextCursor(cursor);
    if (ensureVisible) {
        view->ensureCursorVisible();
    }
}

} // namespace VexaraEditor
