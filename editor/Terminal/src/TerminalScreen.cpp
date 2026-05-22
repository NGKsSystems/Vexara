#include "VexaraEditor/TerminalScreen.h"

namespace VexaraEditor {

namespace {

constexpr int kMaxRows = 5000;

} // namespace

namespace {

void initRow(QVector<TerminalCell>& row, int columns)
{
    row.resize(columns);
    for (int col = 0; col < columns; ++col) {
        row[col] = TerminalCell{};
        row[col].ch = QLatin1Char(' ');
        row[col].fg = 7;
    }
}

void resizeGrid(QVector<QVector<TerminalCell>>& grid,
                int newColumns,
                int newRows,
                const QVector<QVector<TerminalCell>>& oldGrid)
{
    QVector<QVector<TerminalCell>> resized;
    resized.resize(newRows);
    for (int row = 0; row < newRows; ++row) {
        initRow(resized[row], newColumns);
        if (row < oldGrid.size()) {
            const int copyColumns = qMin(newColumns, oldGrid[row].size());
            for (int col = 0; col < copyColumns; ++col) {
                resized[row][col] = oldGrid[row][col];
            }
        }
    }
    grid = std::move(resized);
}

} // namespace

void TerminalScreen::resize(int columns, int rows)
{
    columns_ = qMax(2, columns);
    rows_ = qMax(2, qMin(rows, kMaxRows));

    resizeGrid(cells_, columns_, rows_, cells_);
    if (alternateActive_) {
        resizeGrid(mainCells_, columns_, rows_, mainCells_);
    }
    clampCursor();
}

int TerminalScreen::columns() const
{
    return columns_;
}

int TerminalScreen::rows() const
{
    return rows_;
}

int TerminalScreen::cursorRow() const
{
    return cursorRow_;
}

int TerminalScreen::cursorColumn() const
{
    return cursorColumn_;
}

void TerminalScreen::setDefaultAttributes()
{
    fg_ = 7;
    bg_ = 0;
    bold_ = false;
    underline_ = false;
}

void TerminalScreen::putChar(QChar ch)
{
    if (ch == QLatin1Char('\0')) {
        return;
    }
    if (insertMode_) {
        insertCharacters(1);
    }
    if (cursorColumn_ >= columns_) {
        if (cursorRow_ + 1 < rows_) {
            ++cursorRow_;
        } else {
            scrollUp();
        }
        cursorColumn_ = 0;
    }
    TerminalCell& cell = cellAt(cursorRow_, cursorColumn_);
    cell.ch = ch;
    cell.fg = fg_;
    cell.bg = bg_;
    cell.bold = bold_;
    cell.underline = underline_;
    if (cursorColumn_ + 1 < columns_) {
        ++cursorColumn_;
    } else {
        if (cursorRow_ + 1 < rows_) {
            ++cursorRow_;
        } else {
            scrollUp();
        }
        cursorColumn_ = 0;
    }
}

void TerminalScreen::carriageReturn()
{
    cursorColumn_ = 0;
}

void TerminalScreen::lineFeed()
{
    if (cursorRow_ + 1 < rows_) {
        ++cursorRow_;
    } else {
        scrollUp();
    }
    cursorColumn_ = 0;
}

void TerminalScreen::backspace()
{
    if (cursorColumn_ > 0) {
        --cursorColumn_;
        TerminalCell& cell = cellAt(cursorRow_, cursorColumn_);
        cell.ch = QLatin1Char(' ');
    }
}

void TerminalScreen::cursorUp(int count)
{
    cursorRow_ = qMax(0, cursorRow_ - qMax(1, count));
}

void TerminalScreen::cursorDown(int count)
{
    cursorRow_ = qMin(rows_ - 1, cursorRow_ + qMax(1, count));
}

void TerminalScreen::cursorForward(int count)
{
    cursorColumn_ = qMin(columns_ - 1, cursorColumn_ + qMax(1, count));
}

void TerminalScreen::cursorBack(int count)
{
    cursorColumn_ = qMax(0, cursorColumn_ - qMax(1, count));
}

void TerminalScreen::setCursorPosition(int row, int column)
{
    row = qBound(0, row, rows_ - 1);
    column = qBound(0, column, columns_ - 1);
    if (firstInputRedrawCycle_ && inputOriginColumn_ >= 0 && row == inputOriginRow_
        && column < inputOriginColumn_) {
        if (relativeCupBase_ < 0) {
            relativeCupBase_ = column;
        }
        column = inputOriginColumn_ + (column - relativeCupBase_);
        column = qBound(0, column, columns_ - 1);
        eraseInLine(0);
    }
    cursorRow_ = row;
    cursorColumn_ = column;
}

void TerminalScreen::eraseInLine(int mode)
{
    if (mode == 0) {
        for (int col = cursorColumn_; col < columns_; ++col) {
            cellAt(cursorRow_, col).ch = QLatin1Char(' ');
        }
    } else if (mode == 1) {
        for (int col = 0; col <= cursorColumn_ && col < columns_; ++col) {
            cellAt(cursorRow_, col).ch = QLatin1Char(' ');
        }
    } else if (mode == 2) {
        for (int col = 0; col < columns_; ++col) {
            cellAt(cursorRow_, col).ch = QLatin1Char(' ');
        }
    }
}

void TerminalScreen::insertCharacters(int count)
{
    const int n = qMax(1, count);
    if (cursorRow_ < 0 || cursorRow_ >= rows_) {
        return;
    }
    QVector<TerminalCell>& line = cells_[cursorRow_];
    for (int col = columns_ - 1; col >= cursorColumn_ + n; --col) {
        line[col] = line[col - n];
    }
    for (int col = cursorColumn_; col < cursorColumn_ + n && col < columns_; ++col) {
        line[col] = TerminalCell{};
        line[col].ch = QLatin1Char(' ');
        line[col].fg = fg_;
        line[col].bg = bg_;
        line[col].bold = bold_;
        line[col].underline = underline_;
    }
}

void TerminalScreen::deleteCharacters(int count)
{
    const int n = qMax(1, count);
    if (cursorRow_ < 0 || cursorRow_ >= rows_) {
        return;
    }
    QVector<TerminalCell>& line = cells_[cursorRow_];
    const int from = cursorColumn_;
    const int shiftEnd = columns_ - n;
    for (int col = from; col < shiftEnd; ++col) {
        line[col] = line[col + n];
    }
    for (int col = qMax(from, shiftEnd); col < columns_; ++col) {
        line[col] = TerminalCell{};
        line[col].ch = QLatin1Char(' ');
        line[col].fg = 7;
    }
}

void TerminalScreen::eraseCharacters(int count)
{
    const int n = qMax(1, count);
    if (cursorRow_ < 0 || cursorRow_ >= rows_) {
        return;
    }
    QVector<TerminalCell>& line = cells_[cursorRow_];
    for (int col = cursorColumn_; col < cursorColumn_ + n && col < columns_; ++col) {
        line[col].ch = QLatin1Char(' ');
        line[col].fg = fg_;
        line[col].bg = bg_;
        line[col].bold = bold_;
        line[col].underline = underline_;
    }
}

void TerminalScreen::saveCursor()
{
    savedCursorRow_ = cursorRow_;
    savedCursorColumn_ = cursorColumn_;
}

void TerminalScreen::beginFirstInputRedrawCycle()
{
    firstInputRedrawCycle_ = true;
    inputOriginRow_ = cursorRow_;
    inputOriginColumn_ = cursorColumn_;
    relativeCupBase_ = -1;
}

void TerminalScreen::endFirstInputRedrawCycle()
{
    firstInputRedrawCycle_ = false;
    inputOriginRow_ = -1;
    inputOriginColumn_ = -1;
    relativeCupBase_ = -1;
}

bool TerminalScreen::isFirstInputRedrawCycle() const
{
    return firstInputRedrawCycle_;
}

void TerminalScreen::eraseInLineAfterCarriageReturn()
{
    if (firstInputRedrawCycle_ && inputOriginColumn_ >= 0) {
        cursorColumn_ = inputOriginColumn_;
    }
    eraseInLine(0);
}

void TerminalScreen::restoreCursor()
{
    if (firstInputRedrawCycle_) {
        return;
    }
    if (savedCursorRow_ == cursorRow_ && savedCursorColumn_ > cursorColumn_) {
        return;
    }
    cursorRow_ = qBound(0, savedCursorRow_, rows_ - 1);
    cursorColumn_ = qBound(0, savedCursorColumn_, columns_ - 1);
}

void TerminalScreen::setInsertMode(bool active)
{
    insertMode_ = active;
}

void TerminalScreen::setAlternateScreen(bool active)
{
    Q_UNUSED(active);
    // No-op until full xterm 1049/47 dual-buffer save/restore is implemented.
    // Swapping to an empty alt buffer and restoring blank main discarded the PS prompt.
}

void TerminalScreen::eraseInDisplay(int mode)
{
    if (mode == 0) {
        for (int row = cursorRow_; row < rows_; ++row) {
            for (int col = 0; col < columns_; ++col) {
                if (row == cursorRow_ && col < cursorColumn_) {
                    continue;
                }
                cellAt(row, col).ch = QLatin1Char(' ');
            }
        }
    } else if (mode == 1) {
        for (int row = 0; row <= cursorRow_; ++row) {
            for (int col = 0; col < columns_; ++col) {
                if (row == cursorRow_ && col > cursorColumn_) {
                    continue;
                }
                cellAt(row, col).ch = QLatin1Char(' ');
            }
        }
    } else if (mode == 2) {
        for (int row = 0; row < rows_; ++row) {
            for (int col = 0; col < columns_; ++col) {
                cellAt(row, col).ch = QLatin1Char(' ');
            }
        }
        cursorRow_ = 0;
        cursorColumn_ = 0;
    }
}

void TerminalScreen::applySgr(const QVector<int>& params)
{
    QVector<int> codes = params;
    if (codes.isEmpty()) {
        codes.append(0);
    }

    for (int i = 0; i < codes.size(); ++i) {
        const int code = codes[i];
        if (code == 0) {
            setDefaultAttributes();
        } else if (code == 1) {
            bold_ = true;
        } else if (code == 4) {
            underline_ = true;
        } else if (code == 22) {
            bold_ = false;
        } else if (code == 24) {
            underline_ = false;
        } else if (code >= 30 && code <= 37) {
            fg_ = static_cast<quint8>(code - 30);
        } else if (code >= 40 && code <= 47) {
            bg_ = static_cast<quint8>(code - 40);
        } else if (code >= 90 && code <= 97) {
            fg_ = static_cast<quint8>(code - 90 + 8);
        } else if (code >= 100 && code <= 107) {
            bg_ = static_cast<quint8>(code - 100 + 8);
        } else if (code == 38 && i + 1 < codes.size() && codes[i + 1] == 5 && i + 2 < codes.size()) {
            fg_ = static_cast<quint8>(codes[i + 2] % 16);
            i += 2;
        } else if (code == 48 && i + 1 < codes.size() && codes[i + 1] == 5 && i + 2 < codes.size()) {
            bg_ = static_cast<quint8>(codes[i + 2] % 16);
            i += 2;
        }
    }
}

const QVector<QVector<TerminalCell>>& TerminalScreen::cells() const
{
    return cells_;
}

void TerminalScreen::scrollUp()
{
    if (rows_ <= 1) {
        return;
    }
    for (int row = 1; row < rows_; ++row) {
        cells_[row - 1] = cells_[row];
    }
    cells_[rows_ - 1].fill(TerminalCell{});
    for (int col = 0; col < columns_; ++col) {
        cells_[rows_ - 1][col].ch = QLatin1Char(' ');
        cells_[rows_ - 1][col].fg = 7;
    }
}

void TerminalScreen::clampCursor()
{
    cursorRow_ = qBound(0, cursorRow_, rows_ - 1);
    cursorColumn_ = qBound(0, cursorColumn_, columns_ - 1);
}

TerminalCell& TerminalScreen::cellAt(int row, int column)
{
    return cells_[row][column];
}

} // namespace VexaraEditor
