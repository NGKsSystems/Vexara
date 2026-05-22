#pragma once

#include "VexaraEditor/TerminalCell.h"

#include <QVector>

namespace VexaraEditor {

class TerminalScreen {
public:
    void resize(int columns, int rows);
    int columns() const;
    int rows() const;

    int cursorRow() const;
    int cursorColumn() const;
    int caretRow() const;
    int caretColumn() const;

    void setPsReadLineCompat(bool enabled);
    bool psReadLineCompat() const;

    void setDefaultAttributes();
    void putChar(QChar ch);
    void carriageReturn();
    void lineFeed();
    void backspace();
    void cursorUp(int count);
    void cursorDown(int count);
    void cursorForward(int count);
    void cursorBack(int count);
    void setCursorPosition(int row, int column);
    void eraseInLine(int mode);
    void eraseInDisplay(int mode);
    void insertCharacters(int count);
    void deleteCharacters(int count);
    void eraseCharacters(int count);
    void saveCursor();
    void restoreCursor();
    void beginFirstInputRedrawCycle();
    void endFirstInputRedrawCycle();
    bool isFirstInputRedrawCycle() const;
    void eraseInLineAfterCarriageReturn();
    void setAlternateScreen(bool active);
    void setInsertMode(bool active);
    void applySgr(const QVector<int>& params);

    const QVector<QVector<TerminalCell>>& cells() const;
    const QVector<QVector<TerminalCell>>& scrollback() const;
    void clearHistory();

private:
    void scrollUp();
    void clampCursor();
    TerminalCell& cellAt(int row, int column);

    QVector<QVector<TerminalCell>> scrollback_;
    QVector<QVector<TerminalCell>> cells_;
    int columns_ = 80;
    int rows_ = 24;
    int cursorRow_ = 0;
    int cursorColumn_ = 0;
    int savedCursorRow_ = 0;
    int savedCursorColumn_ = 0;
    bool alternateActive_ = false;
    QVector<QVector<TerminalCell>> mainCells_;
    int mainCursorRow_ = 0;
    int mainCursorColumn_ = 0;
    quint8 mainFg_ = 7;
    quint8 mainBg_ = 0;
    bool mainBold_ = false;
    bool mainUnderline_ = false;
    quint8 fg_ = 7;
    quint8 bg_ = 0;
    bool bold_ = false;
    bool underline_ = false;
    bool insertMode_ = false;
    bool psReadLineCompat_ = false;
    bool firstInputRedrawCycle_ = false;
    QVector<int> rowInputExtent_;
    int inputOriginRow_ = -1;
    int inputOriginColumn_ = -1;
    int relativeCupBase_ = -1;
};

} // namespace VexaraEditor
