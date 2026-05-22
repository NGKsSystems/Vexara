#pragma once

#include <QByteArray>
#include <QChar>
#include <QVector>

namespace VexaraEditor {

class TerminalScreen;

// TEMP: remove after diagnosing PowerShell startup prompt (first ~5s of ConPTY output).
class TerminalStartupDiag {
public:
    static void beginSession();
    static void endSession();
    static bool isLogging();

    static void onRawChunk(int chunkIndex, const QByteArray& chunk);
    static void beforeFeed(int chunkIndex, const TerminalScreen& screen);
    static void afterFeed(const TerminalScreen& screen);

    static void onCarriageReturn(const TerminalScreen& screen);
    static void onLineFeed(const TerminalScreen& screen);
    static void onCsi(char finalByte,
                      const QByteArray& paramsRaw,
                      const QVector<int>& params,
                      const TerminalScreen& screen);
    static void onEscSaveRestore(bool save, const TerminalScreen& screen);
    static void onOscStart(const TerminalScreen& screen);
    static void onOscEnd(const char* how, const QByteArray& oscBuffer, const TerminalScreen& screen);
    static void onPutChar(QChar ch, const TerminalScreen& screen);
};

} // namespace VexaraEditor
