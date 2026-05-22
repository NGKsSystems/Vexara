#pragma once

class QPlainTextEdit;

namespace VexaraEditor {

class TerminalScreen;

class PlainTextTerminalRenderer {
public:
    static int documentPositionForScreen(const TerminalScreen& screen);

    void apply(const TerminalScreen& screen, QPlainTextEdit* view, bool scrollToBottom);
    void placeCaret(const TerminalScreen& screen, QPlainTextEdit* view, bool ensureVisible = true);
};

} // namespace VexaraEditor
