#include "VexaraEditor/VtParser.h"

#include "VexaraEditor/TerminalScreen.h"
#include "VexaraEditor/TerminalStartupDiag.h"

namespace VexaraEditor {

VtParser::VtParser(TerminalScreen& screen)
    : screen_(screen)
{
}

void VtParser::setOnWindowResize(std::function<void(int columns, int rows)> handler)
{
    onWindowResize_ = std::move(handler);
}

void VtParser::reset()
{
    state_ = State::Normal;
    paramsBuffer_.clear();
    utf8Pending_.clear();
    oscBuffer_.clear();
    pendingCr_ = false;
}

void VtParser::flushPendingCrErase()
{
    if (!pendingCr_) {
        return;
    }
    screen_.eraseInLineAfterCarriageReturn();
    pendingCr_ = false;
}

void VtParser::feed(const QByteArray& data)
{
    for (const char byte : data) {
        const unsigned char uch = static_cast<unsigned char>(byte);

        if (pendingCr_ && uch != '\r' && uch != '\n') {
            // PSReadLine often does \r then ESC (save/restore/CUP/EL). Erasing here would
            // wipe the line before those sequences and looks like arrow-key deletion.
            if (uch == 0x1B) {
                pendingCr_ = false;
            } else {
                flushPendingCrErase();
            }
        }

        if (state_ == State::Osc) {
            parseOsc(static_cast<char>(uch));
            continue;
        }

        if (state_ == State::Escape) {
            if (uch == '[') {
                state_ = State::Csi;
                paramsBuffer_.clear();
            } else if (uch == ']') {
                state_ = State::Osc;
                oscBuffer_.clear();
                if (TerminalStartupDiag::isLogging()) {
                    TerminalStartupDiag::onOscStart(screen_);
                }
            } else if (uch == '7') {
                screen_.saveCursor();
                if (TerminalStartupDiag::isLogging()) {
                    TerminalStartupDiag::onEscSaveRestore(true, screen_);
                }
                state_ = State::Normal;
            } else if (uch == '8') {
                pendingCr_ = false;
                screen_.restoreCursor();
                if (TerminalStartupDiag::isLogging()) {
                    TerminalStartupDiag::onEscSaveRestore(false, screen_);
                }
                state_ = State::Normal;
            } else {
                state_ = State::Normal;
            }
            continue;
        }

        if (state_ == State::Csi) {
            if ((uch >= 0x40 && uch <= 0x7E) || uch == '@') {
                executeCsi(static_cast<char>(uch));
                state_ = State::Normal;
                paramsBuffer_.clear();
            } else {
                paramsBuffer_.append(static_cast<char>(uch));
            }
            continue;
        }

        if (uch == 0x1B) {
            state_ = State::Escape;
            continue;
        }

        if (uch == '\r') {
            flushPendingCrErase();
            screen_.carriageReturn();
            pendingCr_ = true;
            if (TerminalStartupDiag::isLogging()) {
                TerminalStartupDiag::onCarriageReturn(screen_);
            }
            utf8Pending_.clear();
            continue;
        }

        if (uch == '\n' || uch == 0x0B || uch == 0x0C) {
            pendingCr_ = false;
            screen_.lineFeed();
            screen_.endFirstInputRedrawCycle();
            if (TerminalStartupDiag::isLogging()) {
                TerminalStartupDiag::onLineFeed(screen_);
            }
            utf8Pending_.clear();
            continue;
        }

        if (uch == '\b') {
            screen_.cursorBack(1);
            utf8Pending_.clear();
            continue;
        }
        if (uch == 0x7F) {
            screen_.backspace();
            utf8Pending_.clear();
            continue;
        }

        if (uch == '\t') {
            const int tabStop = 8;
            const int next = ((screen_.cursorColumn() / tabStop) + 1) * tabStop;
            screen_.setCursorPosition(screen_.cursorRow(), next);
            utf8Pending_.clear();
            continue;
        }

        if (uch < 0x20) {
            continue;
        }

        utf8Pending_.append(static_cast<char>(uch));
        const QString decoded = QString::fromUtf8(utf8Pending_);
        if (decoded.isEmpty() && !utf8Pending_.isEmpty()) {
            continue;
        }
        for (const QChar ch : decoded) {
            if (!ch.isNull()) {
                screen_.putChar(ch);
                if (TerminalStartupDiag::isLogging()) {
                    TerminalStartupDiag::onPutChar(ch, screen_);
                }
            }
        }
        utf8Pending_.clear();
    }
}

void VtParser::executeCsi(char finalByte)
{
    QVector<int> params;
    QByteArray current;
    for (const char ch : paramsBuffer_) {
        if (ch == ';' || ch == ':') {
            params.append(current.isEmpty() ? 0 : current.toInt());
            current.clear();
        } else if ((ch >= '0' && ch <= '9') || ch == '-') {
            current.append(ch);
        }
    }
    params.append(current.isEmpty() ? 0 : current.toInt());

    auto param = [&](int index, int defaultValue) {
        return index < params.size() ? qMax(0, params[index]) : defaultValue;
    };

    if (TerminalStartupDiag::isLogging()) {
        TerminalStartupDiag::onCsi(finalByte, paramsBuffer_, params, screen_);
    }

    switch (finalByte) {
    case 'A':
        screen_.cursorUp(param(0, 1));
        break;
    case 'B':
        screen_.cursorDown(param(0, 1));
        break;
    case 'C':
        screen_.cursorForward(param(0, 1));
        break;
    case 'D':
        screen_.cursorBack(param(0, 1));
        break;
    case 'H':
    case 'f': {
        const int row = param(0, 1) - 1;
        const int col = param(1, 1) - 1;
        screen_.setCursorPosition(row, col);
        break;
    }
    case 'J':
        screen_.eraseInDisplay(param(0, 0));
        break;
    case 'K':
        screen_.eraseInLine(param(0, 0));
        if (param(0, 0) == 0) {
            pendingCr_ = false;
        }
        break;
    case '@':
        screen_.insertCharacters(param(0, 1));
        break;
    case 'P':
        screen_.deleteCharacters(param(0, 1));
        break;
    case 'X':
        screen_.eraseCharacters(param(0, 1));
        break;
    case 's':
        screen_.saveCursor();
        break;
    case 'u':
        pendingCr_ = false;
        screen_.restoreCursor();
        break;
    case 'm':
        screen_.applySgr(params);
        break;
    case 'G':
    case '`':
        screen_.setCursorPosition(screen_.cursorRow(), param(0, 1) - 1);
        break;
    case 'd':
        screen_.setCursorPosition(param(0, 1) - 1, screen_.cursorColumn());
        break;
    case 'e':
        screen_.cursorDown(param(0, 1));
        break;
    case 't':
        if (param(0, 0) == 8 && params.size() >= 3 && onWindowResize_) {
            onWindowResize_(param(2, 80), param(1, 24));
        }
        break;
    case 'h':
    case 'l': {
        const bool enable = finalByte == 'h';
        for (int code : params) {
            if (code == 4) {
                screen_.setInsertMode(enable);
            } else if (code == 1049 || code == 47) {
                screen_.setAlternateScreen(enable);
            }
        }
        break;
    }
    default:
        break;
    }
}

void VtParser::parseOsc(char ch)
{
    if (ch == '\x07') {
        if (TerminalStartupDiag::isLogging()) {
            TerminalStartupDiag::onOscEnd("BEL", oscBuffer_, screen_);
        }
        state_ = State::Normal;
        oscBuffer_.clear();
        return;
    }
    if (ch == 0x1B) {
        if (TerminalStartupDiag::isLogging()) {
            TerminalStartupDiag::onOscEnd("ESC", oscBuffer_, screen_);
        }
        state_ = State::Normal;
        oscBuffer_.clear();
        return;
    }
    oscBuffer_.append(ch);
}

} // namespace VexaraEditor
