#pragma once

#include "VexaraCore/TerminalProfile.h"

#include <QObject>

namespace VexaraEditor {

class PtyReaderThread;
class TerminalScreen;
class VtParser;
class WinConPty;

class TerminalSession : public QObject {
    Q_OBJECT

public:
    explicit TerminalSession(QObject* parent = nullptr);
    ~TerminalSession() override;

    bool isActive() const;
    bool isConPtyMode() const;
    const TerminalScreen& screen() const;
    bool hasPendingCarriageReturn() const;

    bool startConPty(const VexaraCore::TerminalProfile& profile,
                     const QString& workingDirectory,
                     int columns,
                     int rows);
    void stop();
    void writeInput(const QByteArray& data);
    void resize(int columns, int rows);

signals:
    void screenUpdated();
    void sessionEnded(const QString& message);
    void bannerMessage(const QString& message);

private:
    void onBytesReceived(const QByteArray& chunk);
    void flushPendingOutput();
    void checkProcessState();

    WinConPty* pty_ = nullptr;
    PtyReaderThread* reader_ = nullptr;
    TerminalScreen* terminalScreen_ = nullptr;
    VtParser* parser_ = nullptr;
    QByteArray pendingOutput_;
    bool active_ = false;
};

} // namespace VexaraEditor
