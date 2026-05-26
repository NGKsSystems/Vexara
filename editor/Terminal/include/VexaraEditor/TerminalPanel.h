#pragma once

#include "VexaraCore/TerminalProfile.h"

#include <QWidget>

class QKeyEvent;
class QPlainTextEdit;
class QProcess;
class QTimer;

namespace VexaraEditor {

class PlainTextTerminalRenderer;
class TerminalCaretOverlay;
class TerminalSession;

class TerminalPanel : public QWidget {
    Q_OBJECT

public:
    explicit TerminalPanel(QWidget* parent = nullptr);

    void setWorkingDirectory(const QString& path);
    QString workingDirectory() const;

    void setProfile(const VexaraCore::TerminalProfile& profile);
    VexaraCore::TerminalProfile profile() const;
    void restartShell();

    void focusCommandLine();
    void pasteFromClipboard();
    void appendTranscriptOutput(const QString& text);
    bool containsFocusWidget(const QWidget* widget) const;

signals:
    void profileChanged(const QString& profileId);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Backend { None, LineBridge, ConPty };

    void stopBackend();
    bool startConPty();
    bool startLineBridge();
    void refreshView();
    void lockCaretToScreen();
    void updateCaretOverlay();
    void scheduleResize();
    void applyTerminalSize();
    void appendLineBridgeOutput(const QString& text);
    void appendLineBridgePrompt();
    void executeLineBridgeCommand();
    void showSurfaceContextMenu(const QPoint& pos);
    bool handleLineBridgeKey(QKeyEvent* event);
    QString currentLineBridgeInput() const;
    int lineBridgePromptPosition() const;
    int terminalColumns() const;
    int terminalRows() const;

    QPlainTextEdit* surface_ = nullptr;
    TerminalCaretOverlay* caretOverlay_ = nullptr;
    TerminalSession* session_ = nullptr;
    PlainTextTerminalRenderer* renderer_ = nullptr;
    QProcess* lineBridgeProcess_ = nullptr;
    QTimer* resizeDebounce_ = nullptr;
    Backend backend_ = Backend::None;
    QString workingDirectory_;
    VexaraCore::TerminalProfile profile_;
    QString lineBridgePrompt_;
    int lineBridgeReadOnlyEnd_ = 0;
    bool mouseSelecting_ = false;
    bool scrollPinnedToBottom_ = true;
    bool restartingShell_ = false;
};

} // namespace VexaraEditor
