#pragma once

#include <QWidget>

namespace VexaraOrchestration {
class AgentRegistry;
class Orchestrator;
}

class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace VexaraEditor {

class AgentsPanel : public QWidget {
    Q_OBJECT

public:
    explicit AgentsPanel(QWidget* parent = nullptr);

    void bindOrchestrator(VexaraOrchestration::Orchestrator& orchestrator);
    void refresh();

private:
    void submitTask();
    void updateActionState();

    VexaraOrchestration::Orchestrator* orchestrator_ = nullptr;
    QListWidget* list_ = nullptr;
    QPlainTextEdit* planView_ = nullptr;
    QPlainTextEdit* pendingView_ = nullptr;
    QLineEdit* promptEdit_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* approveButton_ = nullptr;
    QPushButton* rejectButton_ = nullptr;
    QPushButton* verifyButton_ = nullptr;
};

} // namespace VexaraEditor
