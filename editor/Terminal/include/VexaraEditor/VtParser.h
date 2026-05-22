#pragma once

#include <QByteArray>

#include <functional>

namespace VexaraEditor {

class TerminalScreen;

class VtParser {
public:
    explicit VtParser(TerminalScreen& screen);

    void feed(const QByteArray& data);
    void reset();
    bool hasPendingCarriageReturn() const { return pendingCr_; }

    void setOnWindowResize(std::function<void(int columns, int rows)> handler);

private:
    enum class State {
        Normal,
        Escape,
        Csi,
        Osc,
    };

    void executeCsi(char finalByte);
    void parseOsc(char ch);
    void flushPendingCrErase();

    TerminalScreen& screen_;
    State state_ = State::Normal;
    QByteArray paramsBuffer_;
    QByteArray utf8Pending_;
    QByteArray oscBuffer_;
    bool pendingCr_ = false;
    std::function<void(int columns, int rows)> onWindowResize_;
};

} // namespace VexaraEditor
