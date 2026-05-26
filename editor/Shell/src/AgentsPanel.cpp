#include "VexaraEditor/AgentsPanel.h"

#include "VexaraEditor/TextContextMenu.h"
#include "VexaraCore/AgentServiceKind.h"
#include "VexaraOrchestration/AgentRegistry.h"
#include "VexaraOrchestration/AgentSnapshot.h"
#include "VexaraOrchestration/Orchestrator.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace VexaraEditor {

namespace {

using VexaraOrchestration::AgentRole;
using VexaraOrchestration::AgentSnapshot;
using VexaraOrchestration::AgentState;

QString stateLabel(AgentState state)
{
    switch (state) {
    case AgentState::Idle:
        return QStringLiteral("Idle");
    case AgentState::Running:
        return QStringLiteral("Running");
    case AgentState::WaitingReview:
        return QStringLiteral("Awaiting review");
    case AgentState::Blocked:
        return QStringLiteral("Blocked");
    }
    return QStringLiteral("Unknown");
}

QString stateTabSuffix(AgentState state)
{
    switch (state) {
    case AgentState::Running:
        return QStringLiteral("●");
    case AgentState::WaitingReview:
        return QStringLiteral("!");
    case AgentState::Blocked:
        return QStringLiteral("✕");
    default:
        return QString();
    }
}

QString stateStyleSheet(AgentState state)
{
    switch (state) {
    case AgentState::Running:
        return QStringLiteral("font-weight: 600; color: #3d8bfd;");
    case AgentState::WaitingReview:
        return QStringLiteral("font-weight: 600; color: #e0a020;");
    case AgentState::Blocked:
        return QStringLiteral("font-weight: 600; color: #e55353;");
    default:
        return QStringLiteral("color: palette(text);");
    }
}

QString filterTranscriptForStage(const QString& transcript, const QStringList& stageMarkers)
{
    if (transcript.trimmed().isEmpty()) {
        return QString();
    }

    const QStringList lines = transcript.split(QLatin1Char('\n'));
    QStringList captured;
    bool inSection = false;

    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("==="))) {
            inSection = false;
            for (const QString& marker : stageMarkers) {
                if (line.contains(marker, Qt::CaseInsensitive)) {
                    inSection = true;
                    break;
                }
            }
        }

        if (inSection) {
            captured.append(line);
        }
    }

    return captured.join(QLatin1Char('\n')).trimmed();
}

QString stageWatchTabName(const QString& stage)
{
    if (stage.contains(QStringLiteral("Planner"), Qt::CaseInsensitive)
        || stage == QStringLiteral("Planning")) {
        return QStringLiteral("Planner");
    }
    if (stage == QStringLiteral("Supervisor review")) {
        return QStringLiteral("Supervisor");
    }
    if (stage.contains(QStringLiteral("Builder"), Qt::CaseInsensitive)
        || stage == QStringLiteral("Worker")) {
        return QStringLiteral("Builder");
    }
    if (stage == QStringLiteral("Testing")) {
        return QStringLiteral("Testing");
    }
    if (stage == QStringLiteral("Review")) {
        return QStringLiteral("Review");
    }
    return QString();
}

QString agentPhaseHint(const QString& tabStageLabel,
                       const QString& activeStage,
                       AgentState agentState,
                       bool pipelineRunning,
                       bool hasPendingReview,
                       bool tabRepresentsBuilder)
{
    const bool isMyStage =
        !activeStage.isEmpty()
        && (activeStage == tabStageLabel
            || (tabStageLabel.contains(QStringLiteral("Planner"), Qt::CaseInsensitive)
                && activeStage.contains(QStringLiteral("Planner"), Qt::CaseInsensitive))
            || (tabStageLabel.contains(QStringLiteral("Builder"), Qt::CaseInsensitive)
                && activeStage.contains(QStringLiteral("Builder"), Qt::CaseInsensitive))
            || (tabStageLabel == QStringLiteral("Testing") && activeStage == QStringLiteral("Testing"))
            || (tabStageLabel == QStringLiteral("Review") && activeStage == QStringLiteral("Review"))
            || (tabStageLabel.contains(QStringLiteral("Supervisor"), Qt::CaseInsensitive)
                && activeStage.contains(QStringLiteral("Supervisor"), Qt::CaseInsensitive)));

    if (hasPendingReview && tabRepresentsBuilder) {
        return QStringLiteral(
            "The pipeline finished. Return to the Master tab, read Pending changes, then click Keep "
            "or Delete.");
    }

    if (agentState == AgentState::Running || isMyStage) {
        return QStringLiteral("This agent is working now — watch the activity log below.");
    }

    if (pipelineRunning && !activeStage.isEmpty() && !isMyStage) {
        const QString watchTab = stageWatchTabName(activeStage);
        if (!watchTab.isEmpty()) {
            return QStringLiteral(
                       "This agent finished its step. The pipeline is on %1 — open the %2 tab for live "
                       "output.")
                .arg(activeStage, watchTab);
        }
        return QStringLiteral(
                   "This agent finished its step. The pipeline is on %1 (Master tab shows details).")
            .arg(activeStage);
    }

    if (agentState == AgentState::Blocked) {
        return QStringLiteral("This step failed or was blocked. Check the log, then use Restart or Delete "
                              "on the Master tab.");
    }

    if (agentState == AgentState::WaitingReview) {
        return QStringLiteral("Waiting for your decision on the Master tab (Keep / Delete).");
    }

    return QStringLiteral("Waiting — this agent runs when the Director reaches its step in the pipeline.");
}

QString defaultPlanHint(bool hierarchical,
                        bool directorActive,
                        bool hierarchicalReady,
                        bool builderReady)
{
    if (hierarchical) {
        if (!directorActive) {
            return QStringLiteral("Open a project folder to activate the pipeline Director and queue.");
        }
        if (!hierarchicalReady) {
            return QStringLiteral(
                "Configure Planner, Supervisor, and Builder backends in Settings, then describe a task below.");
        }
        return QStringLiteral(
            "Director is ready. Enter a task on the Master tab and click Run Task, or queue several tasks in a row.");
    }
    if (!builderReady) {
        return QStringLiteral("Configure the Builder backend in Settings.");
    }
    return QStringLiteral("Legacy pipeline mode — describe a task and click Run Task.");
}

} // namespace

AgentsPanel::AgentsPanel(QWidget* parent)
    : QWidget(parent)
{
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabPosition(QTabWidget::North);

    auto* masterPage = new QWidget(this);
    auto* masterLayout = new QVBoxLayout(masterPage);
    masterLayout->setContentsMargins(4, 4, 4, 4);
    masterLayout->setSpacing(8);

    auto* statusGroup = new QGroupBox(QStringLiteral("Director & queue"), masterPage);
    auto* statusForm = new QFormLayout(statusGroup);
    statusForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    statusForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    masterDirectorValue_ = new QLabel(QStringLiteral("—"), statusGroup);
    masterStageValue_ = new QLabel(QStringLiteral("—"), statusGroup);
    masterQueuePendingValue_ = new QLabel(QStringLiteral("—"), statusGroup);
    masterQueueRootsValue_ = new QLabel(QStringLiteral("—"), statusGroup);
    masterQueueDeferredValue_ = new QLabel(QStringLiteral("—"), statusGroup);
    masterReviewValue_ = new QLabel(QStringLiteral("—"), statusGroup);

    statusForm->addRow(QStringLiteral("Director:"), masterDirectorValue_);
    statusForm->addRow(QStringLiteral("Active stage:"), masterStageValue_);
    statusForm->addRow(QStringLiteral("Queue (pending):"), masterQueuePendingValue_);
    statusForm->addRow(QStringLiteral("Pipelines waiting:"), masterQueueRootsValue_);
    statusForm->addRow(QStringLiteral("Deferred:"), masterQueueDeferredValue_);
    statusForm->addRow(QStringLiteral("Your review:"), masterReviewValue_);

    masterSituationBanner_ = new QLabel(masterPage);
    masterSituationBanner_->setWordWrap(true);
    masterSituationBanner_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    masterSituationBanner_->setStyleSheet(
        QStringLiteral("QLabel { background-color: palette(alternateBase); border: 1px solid "
                       "palette(mid); border-radius: 4px; padding: 8px; }"));
    masterSituationBanner_->setText(
        QStringLiteral("Open a project folder to run the agent pipeline."));

    masterNextStepLabel_ = new QLabel(masterPage);
    masterNextStepLabel_->setWordWrap(true);
    masterNextStepLabel_->setStyleSheet(QStringLiteral("color: palette(mid); padding: 0 4px;"));
    masterNextStepLabel_->setText(
        QStringLiteral("Flow: Planner → Supervisor → Builder → Testing → Review → you (Keep)."));

    auto* progressGroup = new QGroupBox(QStringLiteral("Pipeline progress (live)"), masterPage);
    auto* progressLayout = new QVBoxLayout(progressGroup);
    masterProgressView_ = new QPlainTextEdit(progressGroup);
    masterProgressView_->setReadOnly(true);
    masterProgressView_->setMaximumHeight(160);
    masterProgressView_->setMinimumHeight(120);
    masterProgressView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    masterProgressView_->setPlaceholderText(
        QStringLiteral("Run a task to see each stage, hand-offs, and running timers."));
    installPlainTextContextMenu(masterProgressView_, true);
    progressLayout->addWidget(masterProgressView_);

    masterLogView_ = new QPlainTextEdit(masterPage);
    masterLogView_->setReadOnly(true);
    masterLogView_->setPlaceholderText(
        QStringLiteral("Full pipeline transcript — stages, queue updates, and results."));
    installPlainTextContextMenu(masterLogView_, true);

    auto* pendingGroup =
        new QGroupBox(QStringLiteral("Pending changes (Keep, Restart, or Delete)"), masterPage);
    auto* pendingLayout = new QVBoxLayout(pendingGroup);
    pendingView_ = new QPlainTextEdit(pendingGroup);
    pendingView_->setReadOnly(true);
    pendingView_->setMaximumHeight(88);
    pendingView_->setPlaceholderText(
        QStringLiteral("Summary of pipeline results — use Keep, Restart, or Delete below."));
    installPlainTextContextMenu(pendingView_, true);
    pendingLayout->addWidget(pendingView_);

    auto* taskGroup = new QGroupBox(QStringLiteral("New task"), masterPage);
    auto* taskLayout = new QVBoxLayout(taskGroup);
    promptEdit_ = new QLineEdit(taskGroup);
    promptEdit_->setPlaceholderText(QStringLiteral("Describe what the pipeline should do…"));
    installLineEditContextMenu(promptEdit_);

    hierarchicalPipelineCheck_ =
        new QCheckBox(QStringLiteral("Hierarchical pipeline (Director queue, recommended)"), taskGroup);
    hierarchicalPipelineCheck_->setChecked(true);

    runButton_ = new QPushButton(QStringLiteral("Run Task"), taskGroup);
    runButton_->setDefault(true);

    auto* promptRow = new QHBoxLayout();
    promptRow->addWidget(promptEdit_, 1);
    promptRow->addWidget(runButton_);
    taskLayout->addWidget(hierarchicalPipelineCheck_);
    taskLayout->addLayout(promptRow);

    auto* actionsGroup = new QGroupBox(QStringLiteral("Review & actions"), masterPage);
    auto* actionsLayout = new QHBoxLayout(actionsGroup);
    keepButton_ = new QPushButton(QStringLiteral("Keep"), actionsGroup);
    restartButton_ = new QPushButton(QStringLiteral("Restart"), actionsGroup);
    deleteButton_ = new QPushButton(QStringLiteral("Delete"), actionsGroup);
    keepButton_->setToolTip(
        QStringLiteral("Accept the completed pipeline changes and clear the review."));
    restartButton_->setToolTip(
        QStringLiteral("Discard the current review (if any), resume a paused queue, and re-run "
                       "the last pipeline task."));
    deleteButton_->setToolTip(
        QStringLiteral("Discard pending review changes, or remove all queued root pipelines "
                       "when nothing is awaiting review."));
    verifyButton_ = new QPushButton(QStringLiteral("Verify build"), actionsGroup);
    clearDeferredButton_ = new QPushButton(QStringLiteral("Clear deferred"), actionsGroup);
    clearPendingButton_ = new QPushButton(QStringLiteral("Clear queued"), actionsGroup);
    resumeQueueButton_ = new QPushButton(QStringLiteral("Resume queue"), actionsGroup);
    enqueueThreeTestTasksButton_ = new QPushButton(QStringLiteral("Enqueue 3 test tasks"), actionsGroup);
    actionsLayout->addWidget(keepButton_);
    actionsLayout->addWidget(restartButton_);
    actionsLayout->addWidget(deleteButton_);
    actionsLayout->addWidget(verifyButton_);
    actionsLayout->addWidget(clearDeferredButton_);
    actionsLayout->addWidget(clearPendingButton_);
    actionsLayout->addWidget(resumeQueueButton_);
    actionsLayout->addWidget(enqueueThreeTestTasksButton_);
    actionsLayout->addStretch();

    actionsHintLabel_ = new QLabel(masterPage);
    actionsHintLabel_->setWordWrap(true);
    actionsHintLabel_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 9pt; padding: 0 4px;"));

    auto* setupGroup = new QGroupBox(QStringLiteral("Backends & authentication"), masterPage);
    setupGroup->setCheckable(true);
    setupGroup->setChecked(false);
    auto* setupLayout = new QVBoxLayout(setupGroup);
    grokAuthStatus_ = new QLabel(QStringLiteral("Not configured"), setupGroup);
    openClawStatusLabel_ = new QLabel(setupGroup);
    openClawStatusLabel_->setWordWrap(true);
    openClawStatusLabel_->setVisible(false);
    configureOpenClawOllamaButton_ = new QPushButton(QStringLiteral("Configure for Ollama"), setupGroup);
    configureOpenClawOllamaButton_->setVisible(false);
    loginWithXButton_ = new QPushButton(QStringLiteral("Login with X (Grok)"), setupGroup);
    settingsButton_ = new QPushButton(QStringLiteral("Open settings…"), setupGroup);
    auto* setupButtonRow = new QHBoxLayout();
    setupButtonRow->addWidget(loginWithXButton_);
    setupButtonRow->addWidget(configureOpenClawOllamaButton_);
    setupButtonRow->addWidget(settingsButton_);
    setupButtonRow->addStretch();
    setupLayout->addWidget(grokAuthStatus_);
    setupLayout->addWidget(openClawStatusLabel_);
    setupLayout->addLayout(setupButtonRow);

    masterLayout->addWidget(statusGroup);
    masterLayout->addWidget(masterSituationBanner_);
    masterLayout->addWidget(progressGroup);
    masterLayout->addWidget(masterNextStepLabel_);
    masterLayout->addWidget(taskGroup);
    masterLayout->addWidget(actionsGroup);
    masterLayout->addWidget(actionsHintLabel_);
    masterLayout->addWidget(pendingGroup);
    masterLayout->addWidget(masterLogView_, 1);
    masterLayout->addWidget(setupGroup);

    tabs_->addTab(masterPage, QStringLiteral("Master"));

    plannerTab_ = buildAgentTab(
        QStringLiteral("Planner (Orchestrator) — turns your task into an execution plan."));
    supervisorTab_ = buildAgentTab(
        QStringLiteral("Supervisor — reviews the plan and approves scope before building."));
    builderTab_ = buildAgentTab(
        QStringLiteral("Builder (Worker) — edits files and produces the change set."));
    testingTab_ = buildAgentTab(
        QStringLiteral("Testing — runs the verification command (see Terminal for live output)."));
    reviewTab_ = buildAgentTab(
        QStringLiteral("Reviewer — final quality gate; approves or requests rework."));

    tabs_->addTab(plannerTab_.page, QStringLiteral("Planner"));
    tabs_->addTab(supervisorTab_.page, QStringLiteral("Supervisor"));
    tabs_->addTab(builderTab_.page, QStringLiteral("Builder"));
    tabs_->addTab(testingTab_.page, QStringLiteral("Testing"));
    tabs_->addTab(reviewTab_.page, QStringLiteral("Review"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->addWidget(tabs_);

    connect(hierarchicalPipelineCheck_, &QCheckBox::toggled, this, [this]() {
        updateMasterDashboard();
        updatePipelineGuidance();
        updateActionState();
    });
    connect(settingsButton_, &QPushButton::clicked, this, &AgentsPanel::settingsRequested);
    connect(configureOpenClawOllamaButton_, &QPushButton::clicked, this, [this]() {
        if (!orchestrator_) {
            return;
        }
        QString message;
        if (orchestrator_->configureOpenClawForLocalOllama(&message)) {
            masterLogView_->appendPlainText(QStringLiteral("\n[Vexara] %1\n").arg(message));
        } else if (!message.isEmpty()) {
            masterLogView_->appendPlainText(
                QStringLiteral("\n[Vexara] OpenClaw setup: %1\n").arg(message));
        }
        refresh();
    });
    connect(loginWithXButton_, &QPushButton::clicked, this, &AgentsPanel::loginWithX);
    connect(runButton_, &QPushButton::clicked, this, &AgentsPanel::submitTask);
    connect(keepButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->keepPendingChanges();
            refresh();
        }
    });
    connect(restartButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->restartPipelineWork();
            refresh();
        }
    });
    connect(deleteButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->deletePendingOrQueuedWork();
            refresh();
        }
    });
    connect(verifyButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->runVerification();
        }
    });
    connect(clearDeferredButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->clearDeferredPipelineTasks();
            refresh();
        }
    });
    connect(clearPendingButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->cancelPendingPipelineTasks();
            refresh();
        }
    });
    connect(resumeQueueButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->resumePipelineQueue();
            refresh();
        }
    });
    connect(enqueueThreeTestTasksButton_, &QPushButton::clicked, this, [this]() {
        if (!orchestrator_) {
            return;
        }
        orchestrator_->enqueueThreeTestPipelineTasks();
        refresh();
    });
    connect(promptEdit_, &QLineEdit::returnPressed, this, &AgentsPanel::submitTask);

    progressTicker_ = new QTimer(this);
    progressTicker_->setInterval(1000);
    connect(progressTicker_, &QTimer::timeout, this, &AgentsPanel::updatePipelineProgressDisplay);

    updateGrokStatus();
    updateOpenClawStatus();
    updateMasterDashboard();
    updatePipelineGuidance();
    updatePipelineProgressDisplay();
    updateActionState();
}

AgentsPanel::AgentTabWidgets AgentsPanel::buildAgentTab(const QString& roleDescription)
{
    AgentTabWidgets tab;
    tab.page = new QWidget(this);
    auto* layout = new QVBoxLayout(tab.page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);

    auto* intro = new QLabel(roleDescription, tab.page);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: palette(mid);"));

    tab.phaseHintValue = new QLabel(tab.page);
    tab.phaseHintValue->setWordWrap(true);
    tab.phaseHintValue->setStyleSheet(
        QStringLiteral("QLabel { background-color: palette(alternateBase); border: 1px solid "
                       "palette(mid); border-radius: 4px; padding: 6px; }"));

    auto* statusGroup = new QGroupBox(QStringLiteral("Agent status"), tab.page);
    auto* form = new QFormLayout(statusGroup);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    tab.stateValue = new QLabel(QStringLiteral("—"), statusGroup);
    tab.modelValue = new QLabel(QStringLiteral("—"), statusGroup);
    tab.backendValue = new QLabel(QStringLiteral("—"), statusGroup);
    tab.statusValue = new QLabel(QStringLiteral("—"), statusGroup);
    tab.statusValue->setWordWrap(true);

    form->addRow(QStringLiteral("State:"), tab.stateValue);
    form->addRow(QStringLiteral("Model:"), tab.modelValue);
    form->addRow(QStringLiteral("Backend:"), tab.backendValue);
    form->addRow(QStringLiteral("Status:"), tab.statusValue);

    tab.logView = new QPlainTextEdit(tab.page);
    tab.logView->setReadOnly(true);
    tab.logView->setPlaceholderText(QStringLiteral("Stage activity for this agent appears here."));
    installPlainTextContextMenu(tab.logView, true);

    layout->addWidget(intro);
    layout->addWidget(tab.phaseHintValue);
    layout->addWidget(statusGroup);
    layout->addWidget(new QLabel(QStringLiteral("Activity log"), tab.page));
    layout->addWidget(tab.logView, 1);

    return tab;
}

void AgentsPanel::bindOrchestrator(VexaraOrchestration::Orchestrator& orchestrator)
{
    orchestrator_ = &orchestrator;
    connect(&orchestrator.registry(), &VexaraOrchestration::AgentRegistry::agentsChanged, this,
            &AgentsPanel::scheduleRefresh);
    connect(orchestrator_, &VexaraOrchestration::Orchestrator::taskStateChanged, this,
            &AgentsPanel::scheduleRefresh);
    connect(orchestrator_, &VexaraOrchestration::Orchestrator::verificationStateChanged, this,
            &AgentsPanel::scheduleRefresh);
    connect(orchestrator_, &VexaraOrchestration::Orchestrator::pipelineProgressChanged, this,
            [this]() {
                updatePipelineProgressDisplay();
                syncProgressTicker();
            });
    refresh();
}

void AgentsPanel::scheduleRefresh()
{
    if (!refreshTimer_) {
        refreshTimer_ = new QTimer(this);
        refreshTimer_->setSingleShot(true);
        refreshTimer_->setInterval(150);
        connect(refreshTimer_, &QTimer::timeout, this, &AgentsPanel::refresh);
    }
    refreshTimer_->start();
}

void AgentsPanel::refresh()
{
    if (!orchestrator_) {
        updateGrokStatus();
        updateOpenClawStatus();
        updateMasterDashboard();
        updatePipelineGuidance();
        updatePipelineProgressDisplay();
        updateActionState();
        syncProgressTicker();
        return;
    }

    const VexaraOrchestration::AgentRegistry& registry = orchestrator_->registry();
    QString planText = registry.activePlanSummary();
    if (planText.isEmpty()) {
        planText = defaultPlanHint(hierarchicalPipelineCheck_->isChecked(),
                                   orchestrator_->isDirectorActive(),
                                   orchestrator_->isHierarchicalPipelineReady(),
                                   builderTaskReady());
    }

    const bool planChanged = planText != cachedPlanText_;
    if (planChanged) {
        cachedPlanText_ = planText;
        masterLogView_->setPlainText(planText);
    }

    const QString pendingSummary = registry.pendingChangeSummary();
    if (pendingView_->toPlainText() != pendingSummary) {
        pendingView_->setPlainText(pendingSummary);
    }

    const AgentSnapshot planner = registry.agentById(QStringLiteral("orchestrator-1"));
    const AgentSnapshot supervisor = registry.agentById(QStringLiteral("supervisor-1"));
    const AgentSnapshot builder = registry.agentById(QStringLiteral("builder-1"));

    refreshAgentTab(plannerTab_,
                    1,
                    QStringLiteral("Planner"),
                    planner,
                    AgentRole::Orchestrator,
                    QStringLiteral("Planner (writing plan)"),
                    planText,
                    {QStringLiteral("PLANNING"),
                     QStringLiteral("HIERARCHICAL PIPELINE"),
                     QStringLiteral("QUEUED PIPELINE")},
                    planChanged);
    refreshAgentTab(supervisorTab_,
                    2,
                    QStringLiteral("Supervisor"),
                    supervisor,
                    AgentRole::Supervisor,
                    QStringLiteral("Supervisor review"),
                    planText,
                    {QStringLiteral("SUPERVISOR")},
                    planChanged);
    refreshAgentTab(builderTab_,
                    3,
                    QStringLiteral("Builder"),
                    builder,
                    AgentRole::Builder,
                    QStringLiteral("Builder (editing files)"),
                    planText,
                    {QStringLiteral("WORKER")},
                    planChanged);
    refreshAgentTab(testingTab_,
                    4,
                    QStringLiteral("Testing"),
                    builder,
                    AgentRole::Builder,
                    QStringLiteral("Testing"),
                    planText,
                    {QStringLiteral("TESTING")},
                    planChanged);
    refreshAgentTab(reviewTab_,
                    5,
                    QStringLiteral("Review"),
                    supervisor,
                    AgentRole::Supervisor,
                    QStringLiteral("Review"),
                    planText,
                    {QStringLiteral("REVIEW"),
                     QStringLiteral("PIPELINE COMPLETE"),
                     QStringLiteral("PIPELINE FAILED")},
                    planChanged);

    if (!grokExecutableReady()) {
        grokLoginPending_ = false;
    }
    updateGrokStatus();
    updateOpenClawStatus();
    updateMasterDashboard();
    updatePipelineGuidance();
    updatePipelineProgressDisplay();
    updateActionState();
    syncProgressTicker();
}

void AgentsPanel::updatePipelineProgressDisplay()
{
    if (!masterProgressView_) {
        return;
    }

    const QString progress =
        orchestrator_ ? orchestrator_->formattedPipelineProgress() : QString();
    if (progress == cachedProgressText_) {
        return;
    }

    cachedProgressText_ = progress;
    masterProgressView_->setPlainText(progress);
}

void AgentsPanel::syncProgressTicker()
{
    if (!progressTicker_ || !orchestrator_) {
        return;
    }

    const bool shouldTick =
        orchestrator_->isHierarchicalPipelineRunning() || orchestrator_->isTaskRunning();
    if (shouldTick) {
        if (!progressTicker_->isActive()) {
            progressTicker_->start();
        }
    } else {
        progressTicker_->stop();
    }
}

void AgentsPanel::refreshAgentTab(AgentTabWidgets& tab,
                                  int tabIndex,
                                  const QString& tabTitle,
                                  const AgentSnapshot& agent,
                                  AgentRole role,
                                  const QString& pipelineStageLabel,
                                  const QString& fullTranscript,
                                  const QStringList& stageMarkers,
                                  bool updateLog)
{
    if (!orchestrator_) {
        return;
    }

    const QString suffix = stateTabSuffix(agent.state);
    tabs_->setTabText(tabIndex,
                      suffix.isEmpty() ? tabTitle : QStringLiteral("%1 %2").arg(tabTitle, suffix));

    tab.stateValue->setText(stateLabel(agent.state));
    tab.stateValue->setStyleSheet(stateStyleSheet(agent.state));
    const QString modelDisplay = orchestrator_->effectiveModelDisplayForRole(role);
    tab.modelValue->setText(modelDisplay.isEmpty() ? QStringLiteral("—") : modelDisplay);
    tab.statusValue->setText(agent.statusLine.isEmpty() ? QStringLiteral("—") : agent.statusLine);

    const VexaraCore::AgentServiceKind service = orchestrator_->serviceForRole(role);
    const bool configured = orchestrator_->isRoleBackendConfigured(role);
    if (service == VexaraCore::AgentServiceKind::None) {
        tab.backendValue->setText(QStringLiteral("Not assigned"));
    } else {
        tab.backendValue->setText(
            configured ? VexaraCore::agentServiceKindLabel(service)
                       : QStringLiteral("%1 (not configured)")
                             .arg(VexaraCore::agentServiceKindLabel(service)));
    }

    if (tab.phaseHintValue && orchestrator_) {
        tab.phaseHintValue->setText(
            agentPhaseHint(pipelineStageLabel,
                           orchestrator_->activePipelineStage(),
                           agent.state,
                           orchestrator_->isHierarchicalPipelineRunning(),
                           orchestrator_->hasPendingReview(),
                           role == AgentRole::Builder
                               && pipelineStageLabel.contains(QStringLiteral("Builder"),
                                                              Qt::CaseInsensitive)));
    }

    if (updateLog) {
        const QString filtered = filterTranscriptForStage(fullTranscript, stageMarkers);
        tab.logView->setPlainText(filtered.isEmpty()
                                      ? QStringLiteral("No activity for this stage yet.")
                                      : filtered);
    }
}

void AgentsPanel::updatePipelineGuidance()
{
    if (!masterSituationBanner_ || !masterNextStepLabel_) {
        return;
    }

    const QString flowLine = QStringLiteral(
        "Pipeline order: Planner → Supervisor → Builder → Testing → Review → you (Keep on Master).");

    if (!orchestrator_) {
        masterSituationBanner_->setText(
            QStringLiteral("Not connected — open a project folder to use agents."));
        masterNextStepLabel_->setText(flowLine);
        return;
    }

    const bool hierarchical =
        hierarchicalPipelineCheck_ && hierarchicalPipelineCheck_->isChecked();
    const bool pending = orchestrator_->hasPendingReview();
    const QString stage = orchestrator_->activePipelineStage();
    const bool pipelineRunning = orchestrator_->isHierarchicalPipelineRunning();
    const int rootsWaiting = orchestrator_->pendingRootPipelineCount();

    QString headline;
    QString nextStep;

    if (!hierarchical) {
        headline = orchestrator_->isTaskRunning()
                       ? QStringLiteral("A legacy (non-queued) task is running.")
                       : QStringLiteral("Legacy mode — single Builder path, no Director queue.");
        nextStep = QStringLiteral("Describe a task below and click Run Task.");
    } else if (!orchestrator_->isDirectorActive()) {
        headline = QStringLiteral("No project folder — Director and queue are inactive.");
        nextStep = QStringLiteral(
            "Open a project folder, then configure Planner, Supervisor, and Builder in Settings.");
    } else if (!orchestrator_->isHierarchicalPipelineReady()) {
        headline = QStringLiteral("Pipeline backends are not fully configured.");
        nextStep = QStringLiteral(
            "Open Settings → assign models for Planner, Supervisor, and Builder, then return here.");
    } else if (pending) {
        headline = QStringLiteral("Done — the pipeline finished and needs your decision.");
        nextStep = QStringLiteral(
            "Read Pending changes, then click Keep (accept edits) or Delete (discard). Restart re-runs "
            "the last task.");
    } else if (orchestrator_->isPipelineQueuePaused()) {
        headline =
            QStringLiteral("Queue paused: %1")
                .arg(orchestrator_->pipelineQueuePauseReason().left(160));
        nextStep = QStringLiteral(
            "Fix the backend issue, then click Resume queue or Restart on this tab.");
    } else if (pipelineRunning && !stage.isEmpty()) {
        const QString watchTab = stageWatchTabName(stage);
        headline = QStringLiteral("Running now: %1.").arg(stage);
        if (!watchTab.isEmpty()) {
            headline += QStringLiteral(" Switch to the %1 tab for live output.").arg(watchTab);
        }
        if (stage.contains(QStringLiteral("Builder"), Qt::CaseInsensitive)) {
            nextStep = QStringLiteral(
                "Builder (Aider) is editing files — often the slowest step. Planner showing Idle is "
                "normal; planning is already done.");
        } else if (stage.contains(QStringLiteral("Planner"), Qt::CaseInsensitive)) {
            nextStep = QStringLiteral(
                "Planner is writing the plan. Next: Supervisor review, then Builder edits files.");
        } else if (stage == QStringLiteral("Testing")) {
            nextStep = QStringLiteral("Testing runs your verify/build command — check the Terminal panel.");
        } else {
            nextStep = QStringLiteral(
                "After this step: Builder edits → Testing → Review → you choose Keep or Delete.");
        }
    } else if (rootsWaiting > 0) {
        headline = QStringLiteral("Queued — %1 pipeline(s) waiting; Director will run them in order.")
                       .arg(rootsWaiting);
        nextStep = QStringLiteral(
            "Watch the log below. When a stage starts, the banner and agent tabs update to show which "
            "tab to open.");
    } else {
        headline = QStringLiteral("Idle — ready for a new task.");
        nextStep = QStringLiteral("Describe what the pipeline should do, then click Run Task.");
    }

    masterSituationBanner_->setText(headline);
    masterNextStepLabel_->setText(flowLine + QStringLiteral(" ") + nextStep);

    tabs_->setTabToolTip(0, QStringLiteral("Queue status, actions, and your Keep/Delete decision"));
    if (orchestrator_) {
        const VexaraOrchestration::AgentRegistry& registry = orchestrator_->registry();
        const AgentSnapshot planner = registry.agentById(QStringLiteral("orchestrator-1"));
        const AgentSnapshot supervisor = registry.agentById(QStringLiteral("supervisor-1"));
        const AgentSnapshot builder = registry.agentById(QStringLiteral("builder-1"));
        tabs_->setTabToolTip(
            1,
            agentPhaseHint(QStringLiteral("Planner (writing plan)"),
                           stage,
                           planner.state,
                           pipelineRunning,
                           pending,
                           false));
        tabs_->setTabToolTip(
            2,
            agentPhaseHint(QStringLiteral("Supervisor review"),
                           stage,
                           supervisor.state,
                           pipelineRunning,
                           pending,
                           false));
        tabs_->setTabToolTip(
            3,
            agentPhaseHint(QStringLiteral("Builder (editing files)"),
                           stage,
                           builder.state,
                           pipelineRunning,
                           pending,
                           true));
        tabs_->setTabToolTip(4,
                             agentPhaseHint(QStringLiteral("Testing"),
                                            stage,
                                            builder.state,
                                            pipelineRunning,
                                            pending,
                                            false));
        tabs_->setTabToolTip(5,
                             agentPhaseHint(QStringLiteral("Review"),
                                            stage,
                                            supervisor.state,
                                            pipelineRunning,
                                            pending,
                                            false));
    }
}

void AgentsPanel::updateMasterDashboard()
{
    if (!orchestrator_) {
        masterDirectorValue_->setText(QStringLiteral("Not connected"));
        masterStageValue_->setText(QStringLiteral("—"));
        masterQueuePendingValue_->setText(QStringLiteral("—"));
        masterQueueRootsValue_->setText(QStringLiteral("—"));
        masterQueueDeferredValue_->setText(QStringLiteral("—"));
        masterReviewValue_->setText(QStringLiteral("—"));
        tabs_->setTabText(0, QStringLiteral("Master"));
        updatePipelineGuidance();
        return;
    }

    const bool hierarchical = hierarchicalPipelineCheck_->isChecked();

    if (!hierarchical) {
        masterDirectorValue_->setText(QStringLiteral("Legacy mode (no Director queue)"));
        masterStageValue_->setText(orchestrator_->isTaskRunning() ? QStringLiteral("In progress")
                                                                   : QStringLiteral("Idle"));
        masterQueuePendingValue_->setText(QStringLiteral("n/a"));
        masterQueueRootsValue_->setText(QStringLiteral("n/a"));
        masterQueueDeferredValue_->setText(QStringLiteral("n/a"));
    } else if (!orchestrator_->isDirectorActive()) {
        masterDirectorValue_->setText(QStringLiteral("Inactive — open a project folder"));
        masterStageValue_->setText(QStringLiteral("—"));
        masterQueuePendingValue_->setText(QStringLiteral("—"));
        masterQueueRootsValue_->setText(QStringLiteral("—"));
        masterQueueDeferredValue_->setText(QStringLiteral("—"));
    } else {
        const bool running = orchestrator_->isHierarchicalPipelineRunning();
        if (orchestrator_->isPipelineQueuePaused()) {
            masterDirectorValue_->setText(QStringLiteral("Paused — %1")
                                              .arg(orchestrator_->pipelineQueuePauseReason()
                                                       .left(80)));
        } else {
            masterDirectorValue_->setText(running ? QStringLiteral("Running a pipeline")
                                                 : QStringLiteral("Active — queue idle"));
        }
        const QString stage = orchestrator_->activePipelineStage();
        masterStageValue_->setText(stage.isEmpty() ? QStringLiteral("—") : stage);
        masterQueuePendingValue_->setText(QString::number(orchestrator_->pendingQueueTaskCount()));
        masterQueueRootsValue_->setText(
            QString::number(orchestrator_->pendingRootPipelineCount()));
        masterQueueDeferredValue_->setText(
            QString::number(orchestrator_->deferredQueueTaskCount()));
    }

    if (orchestrator_->hasPendingReview()) {
        masterReviewValue_->setText(QStringLiteral("Changes waiting — Keep, Restart, or Delete"));
        masterReviewValue_->setStyleSheet(QStringLiteral("font-weight: 600; color: #e0a020;"));
    } else {
        masterReviewValue_->setText(QStringLiteral("None — pipeline still running or not finished yet"));
        masterReviewValue_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    }

    QString masterTabTitle = QStringLiteral("Master");
    if (orchestrator_->isHierarchicalPipelineRunning()) {
        masterTabTitle = QStringLiteral("Master ●");
    } else if (hierarchical && orchestrator_->isDirectorActive()
               && orchestrator_->pendingRootPipelineCount() > 0) {
        masterTabTitle =
            QStringLiteral("Master (%1)").arg(orchestrator_->pendingRootPipelineCount());
    }
    tabs_->setTabText(0, masterTabTitle);
}

bool AgentsPanel::grokExecutableReady() const
{
    if (!orchestrator_) {
        return false;
    }
    const QString command = orchestrator_->grokBuildCommand().trimmed();
    return !command.isEmpty() && QFileInfo::exists(command);
}

bool AgentsPanel::builderTaskReady() const
{
    if (!orchestrator_) {
        return false;
    }
    return orchestrator_->isRoleBackendConfigured(AgentRole::Builder);
}

void AgentsPanel::updateGrokStatus()
{
    if (!orchestrator_) {
        grokAuthStatus_->setText(QStringLiteral("Not configured"));
        return;
    }

    const VexaraCore::AgentServiceKind builderService =
        orchestrator_->serviceForRole(AgentRole::Builder);
    if (builderService != VexaraCore::AgentServiceKind::GrokCli) {
        const QString label = VexaraCore::agentServiceKindSettingsLabel(builderService);
        if (builderTaskReady()) {
            grokAuthStatus_->setText(QStringLiteral("Builder backend: %1").arg(label));
        } else {
            const QString modelId =
                orchestrator_->registry().agentById(QStringLiteral("builder-1")).modelId;
            grokAuthStatus_->setText(
                QStringLiteral("Builder (%1) not ready — link profile in Settings (now: %2)")
                    .arg(label, modelId.isEmpty() ? QStringLiteral("none") : modelId));
        }
        return;
    }

    if (!grokExecutableReady()) {
        grokAuthStatus_->setText(QStringLiteral("Grok CLI path not configured"));
        return;
    }
    if (grokLoginPending_) {
        grokAuthStatus_->setText(QStringLiteral("Opening browser for Grok login…"));
        return;
    }
    grokAuthStatus_->setText(QStringLiteral("Grok CLI ready (use Login with X if auth fails)"));
}

void AgentsPanel::updateOpenClawStatus()
{
    const bool showOpenClaw = orchestrator_ && orchestrator_->usesOpenClawForPlannerRoles();
    openClawStatusLabel_->setVisible(showOpenClaw);
    configureOpenClawOllamaButton_->setVisible(showOpenClaw);
    if (!showOpenClaw) {
        return;
    }
    openClawStatusLabel_->setText(orchestrator_->openClawPlannerStatus());
}

void AgentsPanel::submitTask()
{
    const QString text = promptEdit_->text();
    if (text.trimmed().isEmpty()) {
        return;
    }
    const bool useHierarchical = hierarchicalPipelineCheck_->isChecked();
    emit taskRequested(text, useHierarchical);
    promptEdit_->clear();
}

void AgentsPanel::loginWithX()
{
    if (!grokExecutableReady()) {
        updateGrokStatus();
        return;
    }

    const QString command = orchestrator_->grokBuildCommand().trimmed();
    QString workDir = orchestrator_->projectRoot();
    if (workDir.isEmpty()) {
        workDir = QDir::homePath();
    }

    if (!QProcess::startDetached(command, QStringList(), workDir)) {
        grokLoginPending_ = false;
        grokAuthStatus_->setText(QStringLiteral("Failed to launch Grok CLI"));
        return;
    }

    grokLoginPending_ = true;
    updateGrokStatus();
}

void AgentsPanel::updateActionState()
{
    const bool pending = orchestrator_ && orchestrator_->hasPendingReview();
    const bool taskRunning = orchestrator_ && orchestrator_->isTaskRunning();
    const bool grokReady = grokExecutableReady();
    const bool useHierarchical =
        hierarchicalPipelineCheck_ && hierarchicalPipelineCheck_->isChecked();
    const bool canRun =
        orchestrator_
        && (useHierarchical ? orchestrator_->isHierarchicalPipelineReady()
                            : (builderTaskReady() && !taskRunning));

    const bool directorReady = orchestrator_ && orchestrator_->isDirectorActive();
    const bool hasQueuedRoots =
        directorReady && orchestrator_->pendingRootPipelineCount() > 0;
    const bool hasQueueWork =
        directorReady
        && (orchestrator_->pendingQueueTaskCount() > 0 || hasQueuedRoots
            || orchestrator_->deferredQueueTaskCount() > 0);
    const bool queuePaused = directorReady && orchestrator_->isPipelineQueuePaused();
    const bool pipelineBusy =
        orchestrator_
        && (orchestrator_->isHierarchicalPipelineRunning() || taskRunning);
    const bool hasLastTask =
        orchestrator_ && !orchestrator_->lastPipelineUserTask().trimmed().isEmpty();

    keepButton_->setEnabled(pending);
    deleteButton_->setEnabled(
        orchestrator_ != nullptr
        && (pending || queuePaused || hasQueueWork || pipelineBusy));
    restartButton_->setEnabled(
        orchestrator_ != nullptr
        && (pending || queuePaused || hasQueueWork || hasLastTask || pipelineBusy));
    runButton_->setEnabled(canRun);
    promptEdit_->setEnabled(orchestrator_ != nullptr);
    settingsButton_->setEnabled(orchestrator_ != nullptr);
    loginWithXButton_->setEnabled(grokReady && !taskRunning);
    verifyButton_->setEnabled(orchestrator_ != nullptr && !taskRunning);
    clearDeferredButton_->setEnabled(orchestrator_ != nullptr
                                     && orchestrator_->deferredQueueTaskCount() > 0);
    clearPendingButton_->setEnabled(
        orchestrator_ != nullptr && directorReady && orchestrator_->pendingRootPipelineCount() > 0);
    resumeQueueButton_->setEnabled(orchestrator_ != nullptr && directorReady
                                   && orchestrator_->isPipelineQueuePaused());
    enqueueThreeTestTasksButton_->setEnabled(
        orchestrator_ != nullptr && useHierarchical && orchestrator_->isDirectorActive()
        && orchestrator_->isHierarchicalPipelineReady());

    if (!actionsHintLabel_) {
        return;
    }

    if (!orchestrator_) {
        actionsHintLabel_->setText(QString());
        return;
    }

    if (pending) {
        actionsHintLabel_->setText(
            QStringLiteral("Keep — accept edits.  Delete — discard.  Restart — run the last task again."));
    } else if (pipelineBusy) {
        actionsHintLabel_->setText(
            QStringLiteral("Keep unlocks when the pipeline completes.  Delete — stop work and clear "
                           "queue.  Restart — stop and retry the last task."));
    } else if (queuePaused) {
        actionsHintLabel_->setText(
            QStringLiteral("Queue is paused — use Resume queue or Restart after fixing the backend."));
    } else if (hasQueueWork) {
        actionsHintLabel_->setText(
            QStringLiteral("Tasks are queued.  Delete removes waiting pipelines; Restart re-queues the "
                           "last task."));
    } else {
        actionsHintLabel_->setText(
            QStringLiteral("Run a task first.  Keep only applies after a successful pipeline finish."));
    }

    keepButton_->setToolTip(
        pending
            ? QStringLiteral("Accept the completed pipeline changes and clear the review.")
            : QStringLiteral("Available after the pipeline finishes — see the banner above."));
}

} // namespace VexaraEditor
