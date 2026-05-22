#pragma once

#include <QByteArray>

class QKeyEvent;

namespace VexaraEditor {

class TerminalInputEncoder {
public:
    static QByteArray encode(const QKeyEvent& event);
    static bool isCopyShortcut(const QKeyEvent& event);
    static bool isPasteShortcut(const QKeyEvent& event);
    static bool isViewScrollKey(const QKeyEvent& event);
};

} // namespace VexaraEditor
