#pragma once

#include <QChar>

namespace VexaraEditor {

struct TerminalCell {
    QChar ch = QLatin1Char(' ');
    quint8 fg = 7;
    quint8 bg = 0;
    bool bold = false;
    bool underline = false;
};

} // namespace VexaraEditor
