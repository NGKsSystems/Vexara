#include "VexaraEditor/TerminalStartupDiag.h"

#include "VexaraEditor/TerminalScreen.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>

namespace VexaraEditor {

namespace {

constexpr qint64 kDiagWindowMs = 5000;

QElapsedTimer g_timer;
bool g_sessionActive = false;
bool g_firstPromptLikeLogged = false;
bool g_logRowSnapshots = false;
QString g_recentPrint;
int g_chunkCounter = 0;

QString diagLogPath()
{
    QDir proofDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5 && proofDir.dirName() != QStringLiteral("_proof"); ++i) {
        if (!proofDir.cdUp()) {
            break;
        }
    }
    if (proofDir.dirName() != QStringLiteral("_proof")) {
        proofDir.setPath(QCoreApplication::applicationDirPath());
    }
    proofDir.mkpath(QStringLiteral("."));
    return proofDir.absoluteFilePath(QStringLiteral("pty_prompt_startup_diag.txt"));
}

void writeLine(const QString& line)
{
    QFile file(diagLogPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    const qint64 ms = g_sessionActive ? g_timer.elapsed() : 0;
    out << "t=" << ms << "ms " << line << '\n';
}

QString cursorLabel(const TerminalScreen& screen)
{
    return QStringLiteral("cursor=(%1,%2)")
        .arg(screen.cursorRow())
        .arg(screen.cursorColumn());
}

QString previewOsc(const QByteArray& buffer)
{
    const int maxLen = 120;
    QString preview = QString::fromUtf8(buffer.left(maxLen));
    preview.replace(QLatin1Char('\r'), QStringLiteral("<CR>"));
    preview.replace(QLatin1Char('\n'), QStringLiteral("<LF>"));
    preview.replace(QLatin1Char('\x07'), QStringLiteral("<BEL>"));
    if (buffer.size() > maxLen) {
        preview += QStringLiteral("...");
    }
    return preview;
}

QString previewChunk(const QByteArray& chunk)
{
    QString printable;
    printable.reserve(qMin(chunk.size(), 200));
    for (const char byte : chunk) {
        const unsigned char uch = static_cast<unsigned char>(byte);
        if (uch >= 0x20 && uch < 0x7F) {
            printable.append(QChar::fromLatin1(byte));
        } else if (byte == '\r') {
            printable.append(QStringLiteral("<CR>"));
        } else if (byte == '\n') {
            printable.append(QStringLiteral("<LF>"));
        } else if (byte == 0x1B) {
            printable.append(QStringLiteral("<ESC>"));
        } else if (byte == '\t') {
            printable.append(QStringLiteral("<TAB>"));
        }
        if (printable.size() >= 200) {
            printable.append(QStringLiteral("..."));
            break;
        }
    }
    return printable;
}

bool chunkLooksPromptLike(const QByteArray& chunk)
{
    const QString text = QString::fromUtf8(chunk);
    if (text.contains(QStringLiteral("PS ")) || text.contains(QStringLiteral("PS>"))) {
        return true;
    }
    if (text.contains(QLatin1Char('>')) && (text.contains(QStringLiteral(":\\"))
                                            || text.contains(QStringLiteral("]133")))) {
        return true;
    }
    return false;
}

QString csiDescription(char finalByte, const QVector<int>& params)
{
    auto p = [&](int index, int defaultValue) {
        return index < params.size() ? params[index] : defaultValue;
    };

    switch (finalByte) {
    case 'H':
    case 'f':
        return QStringLiteral("CSI_%1 row=%2 col=%3")
            .arg(QLatin1Char(finalByte))
            .arg(p(0, 1))
            .arg(p(1, 1));
    case 'K':
        return QStringLiteral("CSI_K mode=%1").arg(p(0, 0));
    case 'G':
    case '`':
    case 'd':
        return QStringLiteral("CSI_%1 col=%2").arg(QLatin1Char(finalByte)).arg(p(0, 1));
    case 'm':
        return QStringLiteral("CSI_m SGR params=%1").arg(params.size());
    case 's':
        return QStringLiteral("CSI_s save_cursor");
    case 'u':
        return QStringLiteral("CSI_u restore_cursor");
    default:
        return QStringLiteral("CSI_%1").arg(QLatin1Char(finalByte));
    }
}

} // namespace

void TerminalStartupDiag::beginSession()
{
    g_sessionActive = true;
    g_firstPromptLikeLogged = false;
    g_logRowSnapshots = false;
    g_recentPrint.clear();
    g_chunkCounter = 0;
    g_timer.start();

    QFile file(diagLogPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== TEMP prompt startup diag (remove TerminalStartupDiag.* when done) ===\n";
        out << "log_file=" << diagLogPath() << '\n';
        out << "window_ms=" << kDiagWindowMs << '\n';
        out << "session_begin\n";
    }
}

void TerminalStartupDiag::endSession()
{
    if (g_sessionActive) {
        writeLine(QStringLiteral("session_end"));
    }
    g_sessionActive = false;
}

bool TerminalStartupDiag::isLogging()
{
    return g_sessionActive && g_timer.elapsed() < kDiagWindowMs;
}

void TerminalStartupDiag::onRawChunk(int chunkIndex, const QByteArray& chunk)
{
    if (!isLogging()) {
        return;
    }

    const bool hasGt = chunk.indexOf('>') >= 0;
    const bool hasPs = chunk.contains("PS ");
    const bool has133 = chunk.contains("133");
    const bool hasOsc8 = chunk.contains("]8");

    writeLine(QStringLiteral("RAW_CHUNK #%1 size=%2 hasPS=%3 hasGT=%4 has133=%5 hasOSC8=%6 preview=\"%7\"")
                  .arg(chunkIndex)
                  .arg(chunk.size())
                  .arg(hasPs ? 1 : 0)
                  .arg(hasGt ? 1 : 0)
                  .arg(has133 ? 1 : 0)
                  .arg(hasOsc8 ? 1 : 0)
                  .arg(previewChunk(chunk)));

    if (!g_firstPromptLikeLogged && chunkLooksPromptLike(chunk)) {
        g_firstPromptLikeLogged = true;
        g_logRowSnapshots = true;
        writeLine(QStringLiteral("*** FIRST_PROMPT_LIKE_CHUNK #%1 ***").arg(chunkIndex));
    }
    if (hasGt || has133) {
        g_logRowSnapshots = true;
    }
}

QString rowTextAtCursor(const TerminalScreen& screen)
{
    const auto& rows = screen.cells();
    const int row = screen.cursorRow();
    if (row < 0 || row >= rows.size()) {
        return QString();
    }
    QString text;
    const auto& line = rows[row];
    for (const TerminalCell& cell : line) {
        if (cell.ch != QLatin1Char(' ')) {
            text.append(cell.ch);
        } else if (!text.isEmpty()) {
            text.append(QLatin1Char(' '));
        }
    }
    return text.trimmed();
}

void TerminalStartupDiag::beforeFeed(int chunkIndex, const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }
    writeLine(QStringLiteral("BEFORE_FEED chunk=%1 %2").arg(chunkIndex).arg(cursorLabel(screen)));
}

void TerminalStartupDiag::afterFeed(const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }
    writeLine(QStringLiteral("AFTER_FEED %1").arg(cursorLabel(screen)));
    if (g_logRowSnapshots) {
        const QString rowText = rowTextAtCursor(screen);
        if (!rowText.isEmpty()) {
            writeLine(QStringLiteral("ROW_AT_CURSOR \"%1\"").arg(rowText));
        }
    }
}

void TerminalStartupDiag::onCarriageReturn(const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }
    writeLine(QStringLiteral("CR %1").arg(cursorLabel(screen)));
}

void TerminalStartupDiag::onLineFeed(const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }
    writeLine(QStringLiteral("LF %1").arg(cursorLabel(screen)));
}

void TerminalStartupDiag::onCsi(char finalByte,
                                const QByteArray& paramsRaw,
                                const QVector<int>& params,
                                const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }

    const QChar fb = QLatin1Char(finalByte);
    const bool promptRelated = fb == QLatin1Char('K') || fb == QLatin1Char('H') || fb == QLatin1Char('f')
                               || fb == QLatin1Char('G') || fb == QLatin1Char('`') || fb == QLatin1Char('d')
                               || fb == QLatin1Char('m') || fb == QLatin1Char('s') || fb == QLatin1Char('u');
    if (!promptRelated) {
        return;
    }

    writeLine(QStringLiteral("%1 paramsRaw=\"%2\" %3")
                  .arg(csiDescription(finalByte, params))
                  .arg(QString::fromLatin1(paramsRaw))
                  .arg(cursorLabel(screen)));
}

void TerminalStartupDiag::onEscSaveRestore(bool save, const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }
    writeLine(QStringLiteral("%1 %2")
                  .arg(save ? QStringLiteral("ESC_7_save") : QStringLiteral("ESC_8_restore"))
                  .arg(cursorLabel(screen)));
}

void TerminalStartupDiag::onOscStart(const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }
    writeLine(QStringLiteral("OSC_START %1").arg(cursorLabel(screen)));
}

void TerminalStartupDiag::onOscEnd(const char* how,
                                   const QByteArray& oscBuffer,
                                   const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }
    writeLine(QStringLiteral("OSC_END via=%1 buffer=\"%2\" %3")
                  .arg(QString::fromLatin1(how))
                  .arg(previewOsc(oscBuffer))
                  .arg(cursorLabel(screen)));
}

void TerminalStartupDiag::onPutChar(QChar ch, const TerminalScreen& screen)
{
    if (!isLogging()) {
        return;
    }

    g_recentPrint.append(ch);
    if (g_recentPrint.size() > 80) {
        g_recentPrint.remove(0, g_recentPrint.size() - 80);
    }

    const bool interesting = ch == QLatin1Char('>') || g_recentPrint.endsWith(QStringLiteral("PS "))
                             || g_recentPrint.contains(QStringLiteral("PS C"));
    if (!interesting) {
        return;
    }

    writeLine(QStringLiteral("PUTCHAR '%1' recent=\"%2\" %3")
                  .arg(ch)
                  .arg(g_recentPrint)
                  .arg(cursorLabel(screen)));
}

} // namespace VexaraEditor
