#pragma once

#include <QWidget>

class QPlainTextEdit;
class QTimer;

namespace VexaraEditor {

class TerminalScreen;

class TerminalCaretOverlay : public QWidget {
    Q_OBJECT

public:
    explicit TerminalCaretOverlay(QPlainTextEdit* surface, QWidget* parent = nullptr);

    void syncToScreen(const TerminalScreen& screen);
    void syncToDocumentPosition(int position);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPlainTextEdit* surface_ = nullptr;
    QTimer* blinkTimer_ = nullptr;
    bool caretVisible_ = true;
};

} // namespace VexaraEditor
