#include "VexaraEditor/AgentsPanel.h"

#include "VexaraEditor/TextContextMenu.h"
#include "VexaraOrchestration/AgentRegistry.h"
#include "VexaraOrchestration/AgentSnapshot.h"
#include "VexaraOrchestration/Orchestrator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace VexaraEditor {

namespace {

QString roleLabel(VexaraOrchestration::AgentRole role)
{
    switch (role) {
    case VexaraOrchestration::AgentRole::Builder:
        return QStringLiteral("Builder");
    case VexaraOrchestration::AgentRole::Supervisor:
        return QStringLiteral("Supervisor");
    case VexaraOrchestration::AgentRole::Orchestrator:
        return QStringLiteral("Orchestrator");
    }
    return QStringLiteral("Unknown");
}

QString stateLabel(VexaraOrchestration::AgentState state)
{
    switch (state) {
    case VexaraOrchestration::AgentState::Idle:
        return QStringLiteral("idle");
    case VexaraOrchestration::AgentState::Running:
        return QStringLiteral("running");
    case VexaraOrchestration::AgentState::WaitingReview:
        return QStringLiteral("review");
    case VexaraOrchestration::AgentState::Blocked:
        return QStringLiteral("blocked");
    }
    return QStringLiteral("unknown");
}

} // namespace

AgentsPanel::AgentsPanel(QWidget* parent)
    : QWidget(parent)
{
    list_ = new QListWidget(this);
    planView_ = new QPlainTextEdit(this);
    planView_->setReadOnly(true);
    planView_->setMaximumHeight(72);
    planView_->setPlaceholderText(QStringLiteral("Active plan / task prompt"));

    pendingView_ = new QPlainTextEdit(this);
    pendingView_->setReadOnly(true);
    pendingView_->setMaximumHeight(96);
    pendingView_->setPlaceholderText(QStringLiteral("Pending changes for review"));

    promptEdit_ = new QLineEdit(this);
    promptEdit_->setPlaceholderText(QStringLiteral("Describe a task for Grok Build..."));

    runButton_ = new QPushButton(QStringLiteral("Run Task"), this);
    approveButton_ = new QPushButton(QStringLiteral("Approve"), this);
    rejectButton_ = new QPushButton(QStringLiteral("Reject"), this);
    verifyButton_ = new QPushButton(QStringLiteral("Verify"), this);

    auto* actionRow = new QHBoxLayout();
    actionRow->addWidget(runButton_);
    actionRow->addWidget(approveButton_);
    actionRow->addWidget(rejectButton_);
    actionRow->addWidget(verifyButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(new QLabel(QStringLiteral("Agents"), this));
    layout->addWidget(list_, 1);
    layout->addWidget(new QLabel(QStringLiteral("Plan"), this));
    layout->addWidget(planView_);
    layout->addWidget(new QLabel(QStringLiteral("Pending"), this));
    layout->addWidget(pendingView_);
    layout->addWidget(promptEdit_);
    layout->addLayout(actionRow);

    connect(runButton_, &QPushButton::clicked, this, &AgentsPanel::submitTask);
    connect(approveButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->approvePendingChanges();
        }
    });
    connect(rejectButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->rejectPendingChanges();
        }
    });
    connect(verifyButton_, &QPushButton::clicked, this, [this]() {
        if (orchestrator_) {
            orchestrator_->runVerification();
        }
    });
    connect(promptEdit_, &QLineEdit::returnPressed, this, &AgentsPanel::submitTask);

    installLineEditContextMenu(promptEdit_);
    installPlainTextContextMenu(planView_, true);
    installPlainTextContextMenu(pendingView_, true);
}

void AgentsPanel::bindOrchestrator(VexaraOrchestration::Orchestrator& orchestrator)
{
    orchestrator_ = &orchestrator;
    connect(&orchestrator.registry(), &VexaraOrchestration::AgentRegistry::agentsChanged, this,
            &AgentsPanel::refresh);
    connect(orchestrator_, &VexaraOrchestration::Orchestrator::taskStateChanged, this,
            &AgentsPanel::refresh);
    connect(orchestrator_, &VexaraOrchestration::Orchestrator::verificationStateChanged, this,
            &AgentsPanel::refresh);
    refresh();
}

void AgentsPanel::refresh()
{
    list_->clear();
    if (!orchestrator_) {
        return;
    }

    const VexaraOrchestration::AgentRegistry& registry = orchestrator_->registry();
    for (const auto& agent : registry.agents()) {
        QString modelSuffix;
        if (!agent.modelId.isEmpty()) {
            modelSuffix = QStringLiteral(" model:%1").arg(agent.modelId);
        }
        const QString line = QStringLiteral("%1 [%2] (%3) - %4%5")
                                 .arg(agent.displayName,
                                      roleLabel(agent.role),
                                      stateLabel(agent.state),
                                      agent.statusLine,
                                      modelSuffix);
        list_->addItem(line);
    }

    planView_->setPlainText(registry.activePlanSummary());
    pendingView_->setPlainText(registry.pendingChangeSummary());
    updateActionState();
}

void AgentsPanel::submitTask()
{
    if (!orchestrator_) {
        return;
    }
    orchestrator_->submitTask(promptEdit_->text());
    promptEdit_->clear();
    refresh();
}

void AgentsPanel::updateActionState()
{
    const bool pending = orchestrator_ && orchestrator_->hasPendingReview();
    approveButton_->setEnabled(pending);
    rejectButton_->setEnabled(pending);
}

} // namespace VexaraEditor
