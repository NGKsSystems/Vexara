#pragma once

#include <QString>

namespace VexaraEditor {

// QPlainTextEdit does not interpret VT/ANSI sequences; strip them so paths and prompts stay readable.
inline QString stripTerminalAnsi(const QString& text)
{
    QString out;
    out.reserve(text.size());

    const QChar esc(0x1B);
    int i = 0;
    while (i < text.size()) {
        const QChar ch = text[i];
        if (ch != esc) {
            const ushort code = ch.unicode();
            if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QLatin1Char('\t')
                || code >= 32 || code == 0xA0) {
                out.append(ch);
            }
            ++i;
            continue;
        }

        ++i;
        if (i >= text.size()) {
            break;
        }

        const QChar next = text[i];
        if (next == QLatin1Char('[')) {
            ++i;
            while (i < text.size()) {
                const ushort code = text[i].unicode();
                if (code >= 0x40 && code <= 0x7E) {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }

        if (next == QLatin1Char(']')) {
            ++i;
            while (i < text.size()) {
                if (text[i] == QLatin1Char('\x07')) {
                    ++i;
                    break;
                }
                if (text[i] == esc && i + 1 < text.size() && text[i + 1] == QLatin1Char('\\')) {
                    i += 2;
                    break;
                }
                ++i;
            }
            continue;
        }

        if (next == QLatin1Char('P')) {
            ++i;
            while (i < text.size()) {
                if (text[i] == esc && i + 1 < text.size() && text[i + 1] == QLatin1Char('\\')) {
                    i += 2;
                    break;
                }
                ++i;
            }
            continue;
        }

        if (next == QLatin1Char('(') || next == QLatin1Char(')') || next == QLatin1Char('*')
            || next == QLatin1Char('+')) {
            i += 2;
            continue;
        }

        ++i;
    }

    return out;
}

inline QString decodeTerminalBytes(const QByteArray& bytes)
{
    QString text = QString::fromUtf8(bytes);
    if (text.isEmpty() && !bytes.isEmpty()) {
        text = QString::fromLocal8Bit(bytes);
    }
    return stripTerminalAnsi(text);
}

} // namespace VexaraEditor
