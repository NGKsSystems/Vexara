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

int firstNonEmptyRow(const QVector<QVector<TerminalCell>>& rows)
{
    for (int row = 0; row < rows.size(); ++row) {
        if (!isRowBlank(rows[row])) {
            return row;
        }
    }
    return -1;
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

} // namespace

int PlainTextTerminalRenderer::documentPositionForScreen(const TerminalScreen& screen)
{
    const auto& rows = screen.cells();
    const int firstRow = firstNonEmptyRow(rows);
    if (firstRow < 0) {
        return 0;
    }

    const int cursorRow = qBound(0, screen.cursorRow(), rows.size() - 1);
    const int cursorCol = qBound(0, screen.cursorColumn(), rows[cursorRow].size());
    return documentOffsetForRowCol(rows, firstRow, cursorRow, cursorCol, cursorRow, cursorCol);
}

void PlainTextTerminalRenderer::apply(const TerminalScreen& screen, QPlainTextEdit* view, bool scrollToBottom)
{
    if (!view) {
        return;
    }

    const bool atBottom = scrollToBottom
                          && view->verticalScrollBar()->value() >= view->verticalScrollBar()->maximum() - 2;

    QTextCursor cursor(view->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();

    const auto& rows = screen.cells();
    const int firstRow = firstNonEmptyRow(rows);
    const int lastRow = lastNonEmptyRow(rows);
    const int cursorRow = rows.isEmpty() ? 0 : qBound(0, screen.cursorRow(), rows.size() - 1);
    const int cursorCol = rows.isEmpty() ? 0 : qBound(0, screen.cursorColumn(), rows[cursorRow].size());

    if (firstRow >= 0 && lastRow >= firstRow) {
        for (int row = firstRow; row <= lastRow; ++row) {
            const auto& line = rows[row];
            const int length = lineDisplayLength(line, row, cursorRow, cursorCol);
            for (int col = 0; col < length; ++col) {
                cursor.insertText(QString(line[col].ch), formatForCell(line[col]));
            }
            if (row < lastRow) {
                cursor.insertText(QStringLiteral("\n"));
            }
        }
    }
    cursor.endEditBlock();

    if (atBottom || firstRow >= 0) {
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
    }
}

void PlainTextTerminalRenderer::placeCaret(const TerminalScreen& screen, QPlainTextEdit* view)
{
    if (!view) {
        return;
    }

    const int position = PlainTextTerminalRenderer::documentPositionForScreen(screen);
    const int clamped = qBound(0, position, view->document()->characterCount());

    QTextCursor cursor(view->document());
    cursor.setPosition(clamped);
    view->setTextCursor(cursor);
    view->ensureCursorVisible();
}

} // namespace VexaraEditor
