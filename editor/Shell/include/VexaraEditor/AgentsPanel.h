#pragma once

#include <QString>

#include <QWidget>

class QTimer;

namespace VexaraOrchestration {
class AgentRegistry;
class AgentSnapshot;
class Orchestrator;
enum class AgentRole;
}

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;

namespace VexaraEditor {

class AgentsPanel : public QWidget {
    Q_OBJECT

public:
    explicit AgentsPanel(QWidget* parent = nullptr);

    void bindOrchestrator(VexaraOrchestration::Orchestrator& orchestrator);
    void refresh();
    void scheduleRefresh();

signals:
    void settingsRequested();
    void taskRequested(const QString& userTask, bool useHierarchicalPipeline);

private:
    struct AgentTabWidgets {
        QWidget* page = nullptr;
        QLabel* phaseHintValue = nullptr;
        QLabel* stateValue = nullptr;
        QLabel* modelValue = nullptr;
        QLabel* backendValue = nullptr;
        QLabel* statusValue = nullptr;
        QPlainTextEdit* logView = nullptr;
    };

    AgentTabWidgets buildAgentTab(const QString& roleDescription);
    void submitTask();
    void loginWithX();
    void updateGrokStatus();
    void updateOpenClawStatus();
    void updateActionState();
    void updateMasterDashboard();
    void updatePipelineGuidance();
    void updatePipelineProgressDisplay();
    void syncProgressTicker();
    void refreshAgentTab(AgentTabWidgets& tab,
                         int tabIndex,
                         const QString& tabTitle,
                         const VexaraOrchestration::AgentSnapshot& agent,
                         VexaraOrchestration::AgentRole role,
                         const QString& pipelineStageLabel,
                         const QString& fullTranscript,
                         const QStringList& stageMarkers,
                         bool updateLog);
    bool grokExecutableReady() const;
    bool builderTaskReady() const;

    VexaraOrchestration::Orchestrator* orchestrator_ = nullptr;
    bool grokLoginPending_ = false;

    QTabWidget* tabs_ = nullptr;

    QLabel* masterDirectorValue_ = nullptr;
    QLabel* masterStageValue_ = nullptr;
    QLabel* masterQueuePendingValue_ = nullptr;
    QLabel* masterQueueRootsValue_ = nullptr;
    QLabel* masterQueueDeferredValue_ = nullptr;
    QLabel* masterReviewValue_ = nullptr;
    QLabel* masterSituationBanner_ = nullptr;
    QLabel* masterNextStepLabel_ = nullptr;
    QLabel* actionsHintLabel_ = nullptr;
    QPlainTextEdit* masterProgressView_ = nullptr;

    QPlainTextEdit* masterLogView_ = nullptr;
    QPlainTextEdit* pendingView_ = nullptr;

    QLineEdit* promptEdit_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QCheckBox* hierarchicalPipelineCheck_ = nullptr;

    QPushButton* keepButton_ = nullptr;
    QPushButton* restartButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* verifyButton_ = nullptr;
    QPushButton* clearDeferredButton_ = nullptr;
    QPushButton* clearPendingButton_ = nullptr;
    QPushButton* resumeQueueButton_ = nullptr;
    QPushButton* enqueueThreeTestTasksButton_ = nullptr;

    QLabel* grokAuthStatus_ = nullptr;
    QLabel* openClawStatusLabel_ = nullptr;
    QPushButton* configureOpenClawOllamaButton_ = nullptr;
    QPushButton* loginWithXButton_ = nullptr;
    QPushButton* settingsButton_ = nullptr;

    AgentTabWidgets plannerTab_;
    AgentTabWidgets supervisorTab_;
    AgentTabWidgets builderTab_;
    AgentTabWidgets testingTab_;
    AgentTabWidgets reviewTab_;

    QTimer* refreshTimer_ = nullptr;
    QTimer* progressTicker_ = nullptr;
    QString cachedPlanText_;
    QString cachedProgressText_;
};

} // namespace VexaraEditor
