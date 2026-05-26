#pragma once

#include <QString>

namespace VexaraOrchestration {

struct ChatMessage {
    QString role;
    QString speaker;
    QString content;
};

} // namespace VexaraOrchestration
