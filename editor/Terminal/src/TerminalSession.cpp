#include "VexaraEditor/TerminalSession.h"

#include "VexaraEditor/PtyReaderThread.h"
#include "VexaraEditor/TerminalScreen.h"
#include "VexaraEditor/TerminalStartupDiag.h"
#include "VexaraEditor/VtParser.h"
#include "VexaraEditor/WinConPty.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

namespace VexaraEditor {

namespace {

// TEMP: remove after diagnosing PowerShell startup prompt. Logs first two PTY output chunks.
int g_ptyStartupChunksLogged = 0;
int g_ptyChunkCounter = 0;
bool g_logNextEditOutputChunk = false;

void logFirstEditPtyChunk(const QByteArray& chunk)
{
    const QString path = QCoreApplication::applicationDirPath()
                         + QStringLiteral("/pty_first_edit_chunk.txt");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out << "size=" << chunk.size() << "\n\n--- printable ---\n";
    for (const char byte : chunk) {
        const unsigned char uch = static_cast<unsigned char>(byte);
        if (uch >= 0x20 && uch < 0x7F && byte != '\r' && byte != '\n') {
            out << QChar::fromLatin1(byte);
        } else if (byte == '\r') {
            out << "<CR>";
        } else if (byte == '\n') {
            out << "<LF>";
        } else if (byte == '\t') {
            out << "<TAB>";
        } else if (byte == 0x1B) {
            out << "<ESC>";
        } else {
            out << '<' << Qt::hex << uch << '>';
        }
    }
    out << "\n\n--- hex ---\n" << Qt::dec;
    for (int i = 0; i < chunk.size(); ++i) {
        const unsigned char uch = static_cast<unsigned char>(chunk.at(i));
        out << QStringLiteral("%1 ").arg(uch, 2, 16, QChar('0')).toUpper();
        if ((i + 1) % 16 == 0) {
            out << '\n';
        }
    }
    out << '\n';
}

void logStartupPtyChunk(int index, const QByteArray& chunk)
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
    const QString proofPath = proofDir.absolutePath();
    QFile file(proofPath + QStringLiteral("/pty_startup_chunk_%1.txt").arg(index));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);
    out << "size=" << chunk.size() << "\n\n--- printable ---\n";
    for (const char byte : chunk) {
        const unsigned char uch = static_cast<unsigned char>(byte);
        if (uch >= 0x20 && uch < 0x7F && byte != '\r' && byte != '\n') {
            out << QChar::fromLatin1(byte);
        } else if (byte == '\r') {
            out << "<CR>";
        } else if (byte == '\n') {
            out << "<LF>";
        } else if (byte == '\t') {
            out << "<TAB>";
        } else if (byte == 0x1B) {
            out << "<ESC>";
        } else {
            out << '<' << Qt::hex << uch << '>';
        }
    }
    out << "\n\n--- hex ---\n" << Qt::dec;
    for (int i = 0; i < chunk.size(); ++i) {
        const unsigned char uch = static_cast<unsigned char>(chunk.at(i));
        out << QStringLiteral("%1 ").arg(uch, 2, 16, QChar('0')).toUpper();
        if ((i + 1) % 16 == 0) {
            out << '\n';
        }
    }
    out << '\n';
}

} // namespace

TerminalSession::TerminalSession(QObject* parent)
    : QObject(parent)
    , pty_(new WinConPty())
    , reader_(new PtyReaderThread(this))
    , terminalScreen_(new TerminalScreen())
    , parser_(new VtParser(*terminalScreen_))
{
    parser_->setOnWindowResize([this](int columns, int rows) {
        if (!active_) {
            return;
        }
        pty_->resize(columns, rows);
        terminalScreen_->resize(columns, rows);
    });
    connect(reader_, &PtyReaderThread::bytesReceived, this, &TerminalSession::onBytesReceived);
    connect(reader_, &PtyReaderThread::readThreadFinished, this, [this]() { checkProcessState(); });
}

TerminalSession::~TerminalSession()
{
    stop();
}

bool TerminalSession::isActive() const
{
    return active_;
}

bool TerminalSession::isConPtyMode() const
{
    return active_;
}

const TerminalScreen& TerminalSession::screen() const
{
    return *terminalScreen_;
}

bool TerminalSession::hasPendingCarriageReturn() const
{
    return parser_->hasPendingCarriageReturn();
}

bool TerminalSession::startConPty(const VexaraCore::TerminalProfile& profile,
                                  const QString& workingDirectory,
                                  int columns,
                                  int rows)
{
    stop();

    terminalScreen_->resize(columns, rows);
    terminalScreen_->clearHistory();
    parser_->reset();
    terminalScreen_->setInsertMode(false);
    const bool psReadLineCompat = profile.id == QStringLiteral("pwsh")
                                  || profile.id == QStringLiteral("powershell");
    terminalScreen_->setPsReadLineCompat(psReadLineCompat);
    g_ptyStartupChunksLogged = 0;
    g_ptyChunkCounter = 0;
    g_logNextEditOutputChunk = false;
    TerminalStartupDiag::beginSession();

    if (!pty_->start(profile.program, profile.args, workingDirectory, columns, rows)) {
        return false;
    }

    reader_->setOutputHandle(pty_->outputReadHandle());
    if (reader_->isRunning()) {
        reader_->stopReading();
        reader_->wait(5000);
    }
    reader_->start();

    active_ = true;

    return true;
}

void TerminalSession::stop()
{
    reader_->stopReading();
    pty_->terminate();
    if (reader_->isRunning()) {
        reader_->wait(5000);
    }
    active_ = false;
    pendingOutput_.clear();
    TerminalStartupDiag::endSession();
}

void TerminalSession::writeInput(const QByteArray& data)
{
    if (!active_ || data.isEmpty()) {
        return;
    }
    if (terminalScreen_->psReadLineCompat() && !terminalScreen_->isFirstInputRedrawCycle()) {
        terminalScreen_->beginFirstInputRedrawCycle();
        g_logNextEditOutputChunk = true;
    }
    pty_->writeInput(data);
}

void TerminalSession::resize(int columns, int rows)
{
    if (!active_) {
        return;
    }
    pty_->resize(columns, rows);
    terminalScreen_->resize(columns, rows);
    emit screenUpdated();
}

void TerminalSession::onBytesReceived(const QByteArray& chunk)
{
    if (chunk.isEmpty()) {
        return;
    }
    pendingOutput_.append(chunk);

    // Parse and commit overflow instead of discarding it (discarding looked like missing scrollback).
    constexpr int kMaxPendingBytes = 1024 * 1024;
    constexpr int kParseChunkBytes = 65536;
    while (pendingOutput_.size() > kMaxPendingBytes) {
        const int toParse = qMin(kParseChunkBytes, pendingOutput_.size() - kMaxPendingBytes / 2);
        const QByteArray part = pendingOutput_.left(toParse);
        pendingOutput_.remove(0, toParse);
        parser_->feed(part);
        emit screenUpdated();
    }

    QTimer::singleShot(0, this, &TerminalSession::flushPendingOutput);
}

void TerminalSession::flushPendingOutput()
{
    if (pendingOutput_.isEmpty()) {
        return;
    }
    const QByteArray chunk = pendingOutput_;
    pendingOutput_.clear();
    if (g_logNextEditOutputChunk) {
        logFirstEditPtyChunk(chunk);
        g_logNextEditOutputChunk = false;
    }
    if (g_ptyStartupChunksLogged < 2) {
        logStartupPtyChunk(g_ptyStartupChunksLogged, chunk);
        ++g_ptyStartupChunksLogged;
    }
    if (TerminalStartupDiag::isLogging()) {
        TerminalStartupDiag::onRawChunk(g_ptyChunkCounter, chunk);
        TerminalStartupDiag::beforeFeed(g_ptyChunkCounter, *terminalScreen_);
    }
    parser_->feed(chunk);
    if (TerminalStartupDiag::isLogging()) {
        TerminalStartupDiag::afterFeed(*terminalScreen_);
    }
    ++g_ptyChunkCounter;
    emit screenUpdated();
    checkProcessState();
}

void TerminalSession::checkProcessState()
{
    if (!active_) {
        return;
    }
    if (!pty_->isRunning()) {
        active_ = false;
        emit sessionEnded(QStringLiteral("Process exited."));
    }
}

} // namespace VexaraEditor
