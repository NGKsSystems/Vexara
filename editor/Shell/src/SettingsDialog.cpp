#include "VexaraEditor/SettingsDialog.h"

#include "VexaraEditor/TextContextMenu.h"
#include "VexaraEditor/ApiKeyValidator.h"
#include "VexaraCore/AgentExecutionSettings.h"
#include "VexaraCore/AgentServiceKind.h"
#include "VexaraCore/GrokBuildSettings.h"
#include "VexaraCore/OpenClawSettings.h"
#include "VexaraCore/OpenRouterSettings.h"
#include "VexaraCore/AiderSettings.h"
#include "VexaraCore/ModelProfile.h"
#include "VexaraCore/Paths.h"
#include "VexaraCore/SecureCredentialStore.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QApplication>
#include <QSet>

#include <algorithm>
#include <QSignalBlocker>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace VexaraEditor {

namespace {

QString grokExecutableStatus(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        return QStringLiteral("Not set");
    }
    if (QFileInfo::exists(path)) {
        return QStringLiteral("Found");
    }
    return QStringLiteral("File not found");
}

} // namespace

SettingsDialog::SettingsDialog(VexaraCore::GlobalSettings& settings, QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
    , keyValidator_(new ApiKeyValidator(this))
{
    setWindowTitle(QStringLiteral("Vexara Settings"));
    resize(600, 560);

    settings_.models.ensureDefaults();
    settings_.grokBuild.ensureDefaults();
    settings_.agentExecution.ensureDefaults();

    auto* rolesGroup = new QGroupBox(QStringLiteral("Agent Roles"), this);
    orchestratorBackendCombo_ = new QComboBox(rolesGroup);
    builderBackendCombo_ = new QComboBox(rolesGroup);
    supervisorBackendCombo_ = new QComboBox(rolesGroup);
    populateBackendCombo(orchestratorBackendCombo_);
    populateBackendCombo(builderBackendCombo_);
    populateBackendCombo(supervisorBackendCombo_);

    orchestratorModelCombo_ = new QComboBox(rolesGroup);
    orchestratorModelCombo_->setEditable(true);
    builderModelCombo_ = new QComboBox(rolesGroup);
    builderModelCombo_->setEditable(true);
    supervisorModelCombo_ = new QComboBox(rolesGroup);
    supervisorModelCombo_->setEditable(true);

    auto* rolesForm = new QFormLayout(rolesGroup);
    rolesForm->addRow(QStringLiteral("Orchestrator backend:"), orchestratorBackendCombo_);
    rolesForm->addRow(QStringLiteral("Orchestrator model:"), orchestratorModelCombo_);
    rolesForm->addRow(QStringLiteral("Builder backend:"), builderBackendCombo_);
    rolesForm->addRow(QStringLiteral("Builder model:"), builderModelCombo_);
    rolesForm->addRow(QStringLiteral("Supervisor backend:"), supervisorBackendCombo_);
    rolesForm->addRow(QStringLiteral("Supervisor model:"), supervisorModelCombo_);

    refreshModelsButton_ = new QPushButton(QStringLiteral("Refresh model lists"), rolesGroup);
    modelCatalogStatusLabel_ = new QLabel(rolesGroup);
    modelCatalogStatusLabel_->setWordWrap(true);
    modelCatalogStatusLabel_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    auto* refreshRow = new QHBoxLayout();
    refreshRow->addWidget(refreshModelsButton_);
    refreshRow->addWidget(modelCatalogStatusLabel_, 1);
    rolesForm->addRow(QString(), refreshRow);

    auto* rolesHelp = new QLabel(rolesGroup);
    rolesHelp->setWordWrap(true);
    rolesHelp->setText(
        QStringLiteral("Pick the backend and model for each role. OpenClaw and Aider lists come from "
                       "your installed Ollama models and OpenClaw configuration. HTTP backends use "
                       "an API profile from the keys section below. Click Refresh to rescan."));
    rolesForm->addRow(QString(), rolesHelp);

    const auto onBackendChanged = [this]() { populateModelCombosFromSavedSettings(); };
    connect(orchestratorBackendCombo_, &QComboBox::currentIndexChanged, this, onBackendChanged);
    connect(builderBackendCombo_, &QComboBox::currentIndexChanged, this, onBackendChanged);
    connect(supervisorBackendCombo_, &QComboBox::currentIndexChanged, this, onBackendChanged);
    connect(refreshModelsButton_, &QPushButton::clicked, this, [this]() {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        refreshRoleModelCombos(true);
        QApplication::restoreOverrideCursor();
    });
    connect(orchestratorModelCombo_, &QComboBox::currentTextChanged, this,
            [this](const QString& text) {
                if (backendFromCombo(orchestratorBackendCombo_) == VexaraCore::AgentServiceKind::OpenClawCli
                    || backendFromCombo(supervisorBackendCombo_)
                           == VexaraCore::AgentServiceKind::OpenClawCli) {
                    settings_.agentExecution.openclaw.model = text.trimmed();
                    if (backendFromCombo(supervisorBackendCombo_)
                        == VexaraCore::AgentServiceKind::OpenClawCli) {
                        setModelComboValue(supervisorModelCombo_,
                                           VexaraCore::AgentServiceKind::OpenClawCli,
                                           QStringLiteral("supervisor-1"),
                                           text);
                    }
                }
            });
    connect(supervisorModelCombo_, &QComboBox::currentTextChanged, this,
            [this](const QString& text) {
                if (backendFromCombo(supervisorBackendCombo_) == VexaraCore::AgentServiceKind::OpenClawCli
                    || backendFromCombo(orchestratorBackendCombo_)
                           == VexaraCore::AgentServiceKind::OpenClawCli) {
                    settings_.agentExecution.openclaw.model = text.trimmed();
                    if (backendFromCombo(orchestratorBackendCombo_)
                        == VexaraCore::AgentServiceKind::OpenClawCli) {
                        setModelComboValue(orchestratorModelCombo_,
                                           VexaraCore::AgentServiceKind::OpenClawCli,
                                           QStringLiteral("orchestrator-1"),
                                           text);
                    }
                }
            });

    auto* openClawGroup = new QGroupBox(QStringLiteral("OpenClaw CLI (Orchestrator / Supervisor)"), this);
    openClawCommandEdit_ = new QLineEdit(openClawGroup);
    openClawCommandEdit_->setPlaceholderText(QStringLiteral("Path to openclaw.cmd or openclaw.exe"));
    openClawBrowseButton_ = new QPushButton(QStringLiteral("Browse..."), openClawGroup);
    openClawPathStatus_ = new QLabel(openClawGroup);
    openClawAgentIdEdit_ = new QLineEdit(openClawGroup);
    openClawAgentIdEdit_->setPlaceholderText(QStringLiteral("main"));
    openClawArgsEdit_ = new QLineEdit(openClawGroup);
    openClawArgsEdit_->setPlaceholderText(
        QStringLiteral("agent --agent {agent_id} --local --model {model} --message {prompt} --json"));
    openClawConfigureOllamaButton_ = new QPushButton(QStringLiteral("Configure for Ollama"), openClawGroup);

    auto* openClawCommandRow = new QHBoxLayout();
    openClawCommandRow->addWidget(openClawCommandEdit_, 1);
    openClawCommandRow->addWidget(openClawBrowseButton_);
    openClawCommandRow->addWidget(openClawPathStatus_);

    auto* openClawForm = new QFormLayout(openClawGroup);
    openClawForm->addRow(QStringLiteral("Executable:"), openClawCommandRow);
    openClawForm->addRow(QStringLiteral("Agent id:"), openClawAgentIdEdit_);
    openClawForm->addRow(QStringLiteral("Arguments:"), openClawArgsEdit_);
    openClawForm->addRow(QString(), openClawConfigureOllamaButton_);
    auto* openClawHelp = new QLabel(openClawGroup);
    openClawHelp->setWordWrap(true);
    openClawHelp->setText(
        QStringLiteral(
            "Model is selected under Agent Roles above. Local Ollama mode uses --local and --model "
            "(no OpenAI key). Vexara sets OLLAMA_API_KEY and runs openclaw models set for you.\n"
            "Default path: %1")
            .arg(VexaraCore::OpenClawSettings::defaultInstallPath().isEmpty()
                     ? QStringLiteral("%LOCALAPPDATA%\\Volta\\bin\\openclaw.cmd")
                     : VexaraCore::OpenClawSettings::defaultInstallPath()));
    openClawForm->addRow(QString(), openClawHelp);

    connect(openClawBrowseButton_, &QPushButton::clicked, this, &SettingsDialog::browseOpenClawCommand);
    connect(openClawConfigureOllamaButton_, &QPushButton::clicked, this, &SettingsDialog::configureOpenClawForOllama);
    connect(openClawCommandEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        openClawPathStatus_->setText(grokExecutableStatus(text));
    });

    auto* aiderGroup = new QGroupBox(QStringLiteral("Aider CLI (Builder / Worker)"), this);
    aiderCommandEdit_ = new QLineEdit(aiderGroup);
    aiderCommandEdit_->setPlaceholderText(QStringLiteral("C:\\path\\to\\aider.exe"));
    aiderBrowseButton_ = new QPushButton(QStringLiteral("Browse..."), aiderGroup);
    aiderPathStatus_ = new QLabel(aiderGroup);
    aiderArgsEdit_ = new QLineEdit(aiderGroup);
    aiderArgsEdit_->setPlaceholderText(
        QStringLiteral("--model {model} --yes --skip-sanity-check-repo --message {prompt} "
                       "--exit --no-check-update --no-analytics --no-show-release-notes "
                       "--no-show-model-warnings --no-browser --no-auto-commits --no-git "
                       "--subtree-only"));

    auto* aiderCommandRow = new QHBoxLayout();
    aiderCommandRow->addWidget(aiderCommandEdit_, 1);
    aiderCommandRow->addWidget(aiderBrowseButton_);
    aiderCommandRow->addWidget(aiderPathStatus_);

    auto* aiderForm = new QFormLayout(aiderGroup);
    aiderForm->addRow(QStringLiteral("Executable:"), aiderCommandRow);
    aiderForm->addRow(QStringLiteral("Arguments:"), aiderArgsEdit_);
    auto* aiderHelp = new QLabel(aiderGroup);
    aiderHelp->setWordWrap(true);
    aiderHelp->setText(
        QStringLiteral(
            "Recommended free/local Worker backend. Install: pip install aider-chat\n"
            "Model is selected under Agent Roles (Builder). Run Ollama locally, then pick models like "
            "ollama/qwen2.5-coder:32b or ollama/deepseek-coder-v2.\n"
            "Placeholders: {prompt}, {model}, {cwd}, {files}. Target files are passed via "
            "--file automatically. Aider runs in your open project folder."));
    aiderForm->addRow(QString(), aiderHelp);

    connect(aiderBrowseButton_, &QPushButton::clicked, this, &SettingsDialog::browseAiderCommand);
    connect(aiderCommandEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        aiderPathStatus_->setText(grokExecutableStatus(text));
    });

    auto* grokGroup = new QGroupBox(QStringLiteral("Grok Build CLI"), this);
    grokCommandEdit_ = new QLineEdit(grokGroup);
    grokCommandEdit_->setPlaceholderText(QStringLiteral("Full path to your Grok Build executable"));
    grokBrowseButton_ = new QPushButton(QStringLiteral("Browse..."), grokGroup);
    grokPathStatus_ = new QLabel(grokGroup);
    grokArgsEdit_ = new QLineEdit(grokGroup);
    grokArgsEdit_->setPlaceholderText(QStringLiteral("Default: -p {prompt}  ({prompt} = full composed Grok Build prompt)"));

    auto* grokCommandRow = new QHBoxLayout();
    grokCommandRow->addWidget(grokCommandEdit_, 1);
    grokCommandRow->addWidget(grokBrowseButton_);
    grokCommandRow->addWidget(grokPathStatus_);

    auto* grokForm = new QFormLayout(grokGroup);
    grokForm->addRow(QStringLiteral("Executable:"), grokCommandRow);
    grokForm->addRow(QStringLiteral("Arguments:"), grokArgsEdit_);
    auto* grokHelp = new QLabel(grokGroup);
    grokHelp->setWordWrap(true);
    grokHelp->setText(
        QStringLiteral(
            "Install (PowerShell): irm https://x.ai/cli/install.ps1 | iex\n"
            "Default path: %1\n"
            "Docs: docs/GROK_BUILD_SETUP.md in the Vexara repo.")
            .arg(VexaraCore::GrokBuildSettings::defaultInstallPath().isEmpty()
                     ? QStringLiteral("%USERPROFILE%\\.grok\\bin\\grok.exe")
                     : VexaraCore::GrokBuildSettings::defaultInstallPath()));
    grokForm->addRow(QString(), grokHelp);
    grokForm->addRow(
        QString(),
        new QLabel(QStringLiteral("Vexara runs this in your open project folder when you click Run Task."),
                   grokGroup));

    connect(grokBrowseButton_, &QPushButton::clicked, this, &SettingsDialog::browseGrokCommand);
    connect(grokCommandEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        grokPathStatus_->setText(grokExecutableStatus(text));
    });

    auto* keysGroup = new QGroupBox(QStringLiteral("API keys"), this);
    auto* keysHeader = new QHBoxLayout();
    validateKeysButton_ = new QPushButton(QStringLiteral("Validate all keys"), keysGroup);
    keysHeader->addWidget(validateKeysButton_);
    keysHeader->addStretch();

    apiKeyForm_ = new QFormLayout();
    apiKeyForm_->addRow(
        QString(),
        new QLabel(
            QStringLiteral("API keys are stored in %1, not in vexara.json. Leave a field blank to "
                           "keep an existing key; paste a new value to replace it.")
                .arg(VexaraCore::SecureCredentialStore::storageLabel()),
            keysGroup));

    for (const VexaraCore::ModelProfile& profile : settings_.models.profiles()) {
        auto* keyEdit = new QLineEdit(keysGroup);
        keyEdit->setEchoMode(QLineEdit::Password);
        keyEdit->setPlaceholderText(profile.apiKeyEnv.isEmpty()
                                        ? QStringLiteral("Paste API key to store securely")
                                        : QStringLiteral("Or set env var %1").arg(profile.apiKeyEnv));

        auto* status = new QLabel(QStringLiteral("Not checked"), keysGroup);
        status->setMinimumWidth(140);

        auto* removeButton = new QPushButton(QStringLiteral("Remove"), keysGroup);
        removeButton->setVisible(false);

        auto* row = new QHBoxLayout();
        row->addWidget(keyEdit, 1);
        row->addWidget(removeButton);
        row->addWidget(status);

        const QString label = QStringLiteral("%1 (%2)")
                                  .arg(profile.displayName, VexaraCore::modelProviderLabel(profile.provider));
        apiKeyForm_->addRow(label, row);
        apiKeyEdits_.insert(profile.id, keyEdit);
        apiKeyStatus_.insert(profile.id, status);
        apiKeyRemoveButtons_.insert(profile.id, removeButton);
        installLineEditContextMenu(keyEdit);

        connect(keyEdit, &QLineEdit::textChanged, this, [this, profileId = profile.id]() {
            setKeyStatus(profileId, QStringLiteral("Not checked"), false);
        });
        connect(removeButton, &QPushButton::clicked, this, [this, profileId = profile.id]() {
            removeStoredApiKey(profileId);
        });
    }

    auto* keysLayout = new QVBoxLayout(keysGroup);
    keysLayout->addLayout(keysHeader);
    keysLayout->addLayout(apiKeyForm_);

    auto* verifyGroup = new QGroupBox(QStringLiteral("Verification"), this);
    verifyCommandEdit_ = new QLineEdit(verifyGroup);
    verifyCommandEdit_->setPlaceholderText(QStringLiteral("e.g. tools\\build_release.bat"));
    auto* verifyForm = new QFormLayout(verifyGroup);
    verifyForm->addRow(QStringLiteral("Command:"), verifyCommandEdit_);

    configPathLabel_ = new QLabel(this);
    configPathLabel_->setWordWrap(true);
    configPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    configPathLabel_->setText(
        QStringLiteral("Config file: %1").arg(VexaraCore::Paths::globalConfigPath()));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyFields();
        settings_.save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(validateKeysButton_, &QPushButton::clicked, this, &SettingsDialog::validateAllApiKeys);
    connect(keyValidator_, &ApiKeyValidator::validationStarted, this,
            [this](const QString& profileId) {
                setKeyStatus(profileId, QStringLiteral("Checking..."), false);
                validateKeysButton_->setEnabled(false);
            });
    connect(keyValidator_, &ApiKeyValidator::validationFinished, this,
            [this](const QString& profileId, bool success, const QString& message) {
                setKeyStatus(profileId, message, success);
                validateNextApiKey();
            });

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->addWidget(rolesGroup);
    contentLayout->addWidget(openClawGroup);
    contentLayout->addWidget(aiderGroup);
    contentLayout->addWidget(grokGroup);
    contentLayout->addWidget(keysGroup);
    contentLayout->addWidget(verifyGroup);
    contentLayout->addWidget(configPathLabel_);
    contentLayout->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);

    installLineEditContextMenu(openClawCommandEdit_);
    installLineEditContextMenu(openClawAgentIdEdit_);
    installLineEditContextMenu(openClawArgsEdit_);
    installLineEditContextMenu(aiderCommandEdit_);
    installLineEditContextMenu(aiderArgsEdit_);
    installLineEditContextMenu(grokCommandEdit_);
    installLineEditContextMenu(grokArgsEdit_);
    installLineEditContextMenu(verifyCommandEdit_);

    loadFields();
    QTimer::singleShot(0, this, &SettingsDialog::validateAllApiKeys);
    QTimer::singleShot(100, this, &SettingsDialog::populateModelCombosFromSavedSettings);
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::browseOpenClawCommand()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select OpenClaw executable"),
        openClawCommandEdit_->text().isEmpty() ? QString() : openClawCommandEdit_->text(),
        QStringLiteral("Programs (*.exe *.cmd);;All files (*.*)"));
    if (!path.isEmpty()) {
        openClawCommandEdit_->setText(path);
    }
}

void SettingsDialog::configureOpenClawForOllama()
{
    applyFields();
    if (settings_.agentExecution.openclaw.model.trimmed().isEmpty()) {
        settings_.agentExecution.openclaw.model = QStringLiteral("ollama/qwen2.5-coder:14b");
    }
    settings_.agentExecution.openclaw.localOllamaMode = true;
    const VexaraCore::OpenClawSettings::LocalSetupResult result =
        settings_.agentExecution.openclaw.applyLocalOllamaConfiguration();
    openClawArgsEdit_->setText(settings_.agentExecution.openclaw.args.join(QStringLiteral(" ")));
    setModelComboValue(orchestratorModelCombo_,
                       VexaraCore::AgentServiceKind::OpenClawCli,
                       QStringLiteral("orchestrator-1"),
                       settings_.agentExecution.openclaw.model);
    setModelComboValue(supervisorModelCombo_,
                       VexaraCore::AgentServiceKind::OpenClawCli,
                       QStringLiteral("supervisor-1"),
                       settings_.agentExecution.openclaw.model);
    refreshRoleModelCombos(true);
    QMessageBox::information(this,
                             result.success ? QStringLiteral("OpenClaw ready")
                                            : QStringLiteral("OpenClaw setup"),
                             result.message);
}

void SettingsDialog::browseAiderCommand()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select Aider executable"),
        aiderCommandEdit_->text().isEmpty() ? QString() : aiderCommandEdit_->text(),
        QStringLiteral("Programs (*.exe *.cmd *.bat);;All files (*.*)"));
    if (!path.isEmpty()) {
        aiderCommandEdit_->setText(path);
    }
}

void SettingsDialog::browseGrokCommand()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select Grok Build executable"),
        grokCommandEdit_->text().isEmpty() ? QString() : grokCommandEdit_->text(),
        QStringLiteral("Programs (*.exe);;All files (*.*)"));
    if (!path.isEmpty()) {
        grokCommandEdit_->setText(path);
    }
}

void SettingsDialog::loadFields()
{
    settings_.agentExecution.ensureDefaults();

    const QSignalBlocker blockOrchestrator(orchestratorBackendCombo_);
    const QSignalBlocker blockBuilder(builderBackendCombo_);
    const QSignalBlocker blockSupervisor(supervisorBackendCombo_);

    setBackendCombo(orchestratorBackendCombo_,
                    settings_.agentExecution.serviceForRoleKey(
                        VexaraCore::AgentExecutionSettings::roleKeyOrchestrator()));
    setBackendCombo(builderBackendCombo_,
                    settings_.agentExecution.serviceForRoleKey(
                        VexaraCore::AgentExecutionSettings::roleKeyBuilder()));
    setBackendCombo(supervisorBackendCombo_,
                    settings_.agentExecution.serviceForRoleKey(
                        VexaraCore::AgentExecutionSettings::roleKeySupervisor()));

    settings_.agentExecution.openclaw.ensureDefaults();
    openClawCommandEdit_->setText(settings_.agentExecution.openclaw.command);
    openClawPathStatus_->setText(grokExecutableStatus(settings_.agentExecution.openclaw.command));
    openClawAgentIdEdit_->setText(settings_.agentExecution.openclaw.agentId);
    openClawArgsEdit_->setText(settings_.agentExecution.openclaw.args.join(QStringLiteral(" ")));

    settings_.agentExecution.aider.ensureDefaults();
    aiderCommandEdit_->setText(settings_.agentExecution.aider.command);
    aiderPathStatus_->setText(grokExecutableStatus(settings_.agentExecution.aider.command));
    aiderArgsEdit_->setText(settings_.agentExecution.aider.args.join(QStringLiteral(" ")));

    populateModelCombosFromSavedSettings();

    grokCommandEdit_->setText(settings_.grokBuild.command);
    grokPathStatus_->setText(grokExecutableStatus(settings_.grokBuild.command));
    grokArgsEdit_->setText(settings_.grokBuild.args.join(QStringLiteral(" ")));
    verifyCommandEdit_->setText(settings_.verification.command);

    for (const VexaraCore::ModelProfile& profile : settings_.models.profiles()) {
        refreshKeyFieldUi(profile.id);
    }
}

void SettingsDialog::applyFields()
{
    settings_.agentExecution.setServiceForRoleKey(
        VexaraCore::AgentExecutionSettings::roleKeyOrchestrator(),
        backendFromCombo(orchestratorBackendCombo_));
    settings_.agentExecution.setServiceForRoleKey(
        VexaraCore::AgentExecutionSettings::roleKeyBuilder(),
        backendFromCombo(builderBackendCombo_));
    settings_.agentExecution.setServiceForRoleKey(
        VexaraCore::AgentExecutionSettings::roleKeySupervisor(),
        backendFromCombo(supervisorBackendCombo_));

    settings_.agentExecution.openclaw.command = openClawCommandEdit_->text().trimmed();
    settings_.agentExecution.openclaw.agentId = openClawAgentIdEdit_->text().trimmed();
    if (backendFromCombo(orchestratorBackendCombo_) == VexaraCore::AgentServiceKind::OpenClawCli) {
        settings_.agentExecution.openclaw.model =
            modelValueFromCombo(orchestratorModelCombo_, VexaraCore::AgentServiceKind::OpenClawCli);
    } else if (backendFromCombo(supervisorBackendCombo_)
               == VexaraCore::AgentServiceKind::OpenClawCli) {
        settings_.agentExecution.openclaw.model =
            modelValueFromCombo(supervisorModelCombo_, VexaraCore::AgentServiceKind::OpenClawCli);
    }

    settings_.agentExecution.aider.model =
        modelValueFromCombo(builderModelCombo_, backendFromCombo(builderBackendCombo_));

    const auto assignHttpProfile = [this](const QString& agentId, QComboBox* modelCombo,
                                          VexaraCore::AgentServiceKind backendKind) {
        if (backendKind != VexaraCore::AgentServiceKind::OpenAiHttp
            && backendKind != VexaraCore::AgentServiceKind::OpenRouterHttp
            && backendKind != VexaraCore::AgentServiceKind::GrokCli) {
            return;
        }
        const QString profileId = modelCombo->currentData().toString();
        if (!profileId.isEmpty()) {
            settings_.models.agentModelAssignments.insert(agentId, profileId);
        }
    };
    assignHttpProfile(QStringLiteral("orchestrator-1"),
                      orchestratorModelCombo_,
                      backendFromCombo(orchestratorBackendCombo_));
    assignHttpProfile(QStringLiteral("builder-1"),
                      builderModelCombo_,
                      backendFromCombo(builderBackendCombo_));
    assignHttpProfile(QStringLiteral("supervisor-1"),
                      supervisorModelCombo_,
                      backendFromCombo(supervisorBackendCombo_));
    settings_.agentExecution.openclaw.localOllamaMode = true;
    settings_.agentExecution.openclaw.args =
        VexaraCore::OpenClawSettings::parseArgsField(openClawArgsEdit_->text());
    settings_.agentExecution.openclaw.ensureDefaults();

    settings_.agentExecution.aider.command = aiderCommandEdit_->text().trimmed();
    settings_.agentExecution.aider.args =
        VexaraCore::AiderSettings::parseArgsField(aiderArgsEdit_->text());
    settings_.agentExecution.aider.ensureDefaults();

    settings_.grokBuild.command = grokCommandEdit_->text().trimmed();
    settings_.grokBuild.args = grokArgsEdit_->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    settings_.verification.command = verifyCommandEdit_->text().trimmed();
    settings_.verification.ensureDefaults();

    for (auto it = apiKeyEdits_.cbegin(); it != apiKeyEdits_.cend(); ++it) {
        const QString entered = it.value()->text().trimmed();
        if (entered.isEmpty()) {
            continue;
        }
        settings_.models.setProfileApiKey(it.key(), entered);
        if (VexaraCore::SecureCredentialStore::isAvailable()
            && !VexaraCore::SecureCredentialStore::exists(it.key())) {
            setKeyStatus(it.key(),
                         QStringLiteral("Failed to store key securely."),
                         false);
        } else {
            refreshKeyFieldUi(it.key());
        }
    }
}

void SettingsDialog::validateAllApiKeys()
{
    keyValidator_->cancelAll();
    validationQueue_.clear();

    for (const VexaraCore::ModelProfile& profile : settings_.models.profiles()) {
        if (canValidateProvider(profile.provider)) {
            validationQueue_.append(profile.id);
            setKeyStatus(profile.id, QStringLiteral("Queued"), false);
        } else {
            setKeyStatus(profile.id, QStringLiteral("N/A for this provider"), false);
        }
    }

    validateKeysButton_->setEnabled(false);
    validateNextApiKey();
}

void SettingsDialog::validateNextApiKey()
{
    if (keyValidator_->isBusy()) {
        return;
    }

    while (!validationQueue_.isEmpty()) {
        const QString profileId = validationQueue_.takeFirst();
        const VexaraCore::ModelProfile profile = profileForId(profileId);
        if (!profile.id.isEmpty()) {
            keyValidator_->validateProfile(profile, apiKeyFromField(profileId));
            return;
        }
    }

    validateKeysButton_->setEnabled(true);
}

VexaraCore::ModelProfile SettingsDialog::profileForId(const QString& profileId) const
{
    return settings_.models.profileById(profileId);
}

QString SettingsDialog::apiKeyFromField(const QString& profileId) const
{
    QLineEdit* const edit = apiKeyEdits_.value(profileId);
    return edit ? edit->text().trimmed() : QString();
}

void SettingsDialog::refreshKeyFieldUi(const QString& profileId)
{
    QLineEdit* const edit = apiKeyEdits_.value(profileId);
    if (!edit) {
        return;
    }
    edit->clear();
    const bool stored = VexaraCore::SecureCredentialStore::exists(profileId);
    const VexaraCore::ModelProfile profile = profileForId(profileId);
    if (stored) {
        edit->setPlaceholderText(QStringLiteral("Key stored securely (leave blank to keep)"));
        setKeyStatus(profileId, QStringLiteral("Stored"), true);
    } else {
        edit->setPlaceholderText(profile.apiKeyEnv.isEmpty()
                                   ? QStringLiteral("Paste API key to store securely")
                                   : QStringLiteral("Or set env var %1").arg(profile.apiKeyEnv));
    }
    QPushButton* const removeButton = apiKeyRemoveButtons_.value(profileId);
    if (removeButton) {
        removeButton->setVisible(stored);
    }
}

void SettingsDialog::removeStoredApiKey(const QString& profileId)
{
    settings_.models.clearProfileApiKey(profileId);
    refreshKeyFieldUi(profileId);
    setKeyStatus(profileId, QStringLiteral("Removed"), false);
}

void SettingsDialog::populateBackendCombo(QComboBox* combo) const
{
    if (!combo) {
        return;
    }
    combo->clear();
    for (const VexaraCore::AgentServiceKind kind : VexaraCore::assignableAgentServices()) {
        combo->addItem(VexaraCore::agentServiceKindSettingsLabel(kind),
                       static_cast<int>(kind));
    }
}

VexaraCore::AgentServiceKind SettingsDialog::backendFromCombo(const QComboBox* combo) const
{
    if (!combo || combo->currentIndex() < 0) {
        return VexaraCore::AgentServiceKind::None;
    }
    return static_cast<VexaraCore::AgentServiceKind>(combo->currentData().toInt());
}

void SettingsDialog::setBackendCombo(QComboBox* combo, VexaraCore::AgentServiceKind kind) const
{
    if (!combo) {
        return;
    }
    const int index = combo->findData(static_cast<int>(kind));
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

void SettingsDialog::setKeyStatus(const QString& profileId, const QString& text, bool successHint)
{
    QLabel* const status = apiKeyStatus_.value(profileId);
    if (!status) {
        return;
    }
    status->setText(text);
    if (text == QStringLiteral("Checking...") || text == QStringLiteral("Queued")
        || text == QStringLiteral("Not checked")) {
        status->setStyleSheet(QString());
    } else if (successHint) {
        status->setStyleSheet(QStringLiteral("color: #1a7f37;"));
    } else {
        status->setStyleSheet(QStringLiteral("color: #b42318;"));
    }
}

QStringList SettingsDialog::modelIdsForBackend(VexaraCore::AgentServiceKind backendKind) const
{
    switch (backendKind) {
    case VexaraCore::AgentServiceKind::OpenClawCli: {
        VexaraCore::OpenClawSettings openClaw = settings_.agentExecution.openclaw;
        if (openClawCommandEdit_) {
            openClaw.command = openClawCommandEdit_->text().trimmed();
        }
        return openClaw.discoverOpenClawPickerModels(false).modelIds;
    }
    case VexaraCore::AgentServiceKind::AiderCli:
        return VexaraCore::OpenClawSettings::discoverOllamaPickerModels(
                   settings_.agentExecution.openclaw.ollamaBaseUrl)
            .modelIds;
    case VexaraCore::AgentServiceKind::GrokCli: {
        QStringList ids;
        for (const VexaraCore::ModelProfile& profile : settings_.models.profiles()) {
            if (profile.provider == VexaraCore::ModelProvider::Xai) {
                ids.append(profile.id);
            }
        }
        return ids;
    }
    case VexaraCore::AgentServiceKind::OpenAiHttp: {
        QStringList ids;
        for (const VexaraCore::ModelProfile& profile : settings_.models.profiles()) {
            if (profile.provider == VexaraCore::ModelProvider::OpenAi) {
                ids.append(profile.id);
            }
        }
        return ids;
    }
    case VexaraCore::AgentServiceKind::OpenRouterHttp: {
        QStringList ids;
        for (const VexaraCore::ModelProfile& profile : settings_.models.profiles()) {
            if (profile.provider == VexaraCore::ModelProvider::OpenRouter) {
                ids.append(profile.id);
            }
        }
        return ids;
    }
    case VexaraCore::AgentServiceKind::None:
        break;
    }
    return {};
}

void SettingsDialog::populateModelCombo(QComboBox* combo,
                                        const QStringList& modelIds,
                                        const QString& selectedModel,
                                        bool enabled,
                                        const QString& emptyHint) const
{
    if (!combo) {
        return;
    }

    const QSignalBlocker blocker(combo);
    combo->clear();

    if (!enabled) {
        combo->addItem(emptyHint.isEmpty() ? QStringLiteral("Select a backend first") : emptyHint);
        combo->setEnabled(false);
        return;
    }

    if (modelIds.isEmpty()) {
        combo->addItem(emptyHint.isEmpty() ? QStringLiteral("No models found") : emptyHint);
        combo->setEnabled(true);
        return;
    }

    for (const QString& modelId : modelIds) {
        const VexaraCore::ModelProfile profile = settings_.models.profileById(modelId);
        if (!profile.id.isEmpty()) {
            combo->addItem(QStringLiteral("%1 — %2").arg(profile.displayName, profile.modelName),
                           profile.id);
        } else {
            combo->addItem(modelId, modelId);
        }
    }
    combo->setEnabled(true);

    const QString trimmedSelected = selectedModel.trimmed();
    if (!trimmedSelected.isEmpty()
        && trimmedSelected != QStringLiteral("Select a backend first")) {
        const int dataIndex = combo->findData(trimmedSelected);
        if (dataIndex >= 0) {
            combo->setCurrentIndex(dataIndex);
        } else {
            const int textIndex = combo->findText(trimmedSelected);
            if (textIndex >= 0) {
                combo->setCurrentIndex(textIndex);
            } else {
                combo->setCurrentText(trimmedSelected);
            }
        }
    }
}

void SettingsDialog::populateCliModelCombo(
    QComboBox* combo,
    const VexaraCore::OpenClawSettings::ModelCatalogResult& catalog,
    const QString& selectedModel,
    bool enabled) const
{
    if (!combo) {
        return;
    }

    const QSignalBlocker blocker(combo);
    combo->clear();

    if (!enabled) {
        combo->addItem(QStringLiteral("Select a backend first"));
        combo->setEnabled(false);
        return;
    }

    for (const QString& modelId : catalog.modelIds) {
        combo->addItem(modelId, modelId);
    }

    if (combo->count() == 0) {
        for (const QString& preset : VexaraCore::OpenClawSettings::defaultOllamaModelPresets()) {
            combo->addItem(preset, preset);
        }
    }

    if (combo->count() == 0) {
        combo->addItem(QStringLiteral("No models found — click Refresh"));
    }

    combo->setMaxVisibleItems(qMin(30, combo->count()));
    combo->setEnabled(true);

    const QString trimmedSelected = selectedModel.trimmed();
    if (!trimmedSelected.isEmpty()) {
        const int dataIndex = combo->findData(trimmedSelected);
        if (dataIndex >= 0) {
            combo->setCurrentIndex(dataIndex);
            return;
        }
        combo->addItem(trimmedSelected, trimmedSelected);
        combo->setCurrentIndex(combo->count() - 1);
    }
}

void SettingsDialog::populateOpenRouterModelCombo(QComboBox* combo,
                                                  const VexaraCore::OpenRouterPickerCatalog& catalog,
                                                  const QString& agentId,
                                                  VexaraCore::OpenRouterRoleUse roleUse,
                                                  bool enabled)
{
    if (!combo) {
        return;
    }

    const QSignalBlocker blocker(combo);
    combo->clear();

    if (!enabled) {
        combo->addItem(QStringLiteral("Select a backend first"));
        combo->setEnabled(false);
        return;
    }

    QVector<VexaraCore::OpenRouterModelEntry> sorted = catalog.entries;
    std::sort(sorted.begin(), sorted.end(), [roleUse](const VexaraCore::OpenRouterModelEntry& a,
                                                      const VexaraCore::OpenRouterModelEntry& b) {
        if (a.isFree != b.isFree) {
            return a.isFree > b.isFree;
        }
        const int scoreA = VexaraCore::OpenRouterSettings::roleFitScore(a.id, roleUse);
        const int scoreB = VexaraCore::OpenRouterSettings::roleFitScore(b.id, roleUse);
        if (scoreA != scoreB) {
            return scoreA > scoreB;
        }
        const double costA = a.promptPricePerMillion + a.completionPricePerMillion;
        const double costB = b.promptPricePerMillion + b.completionPricePerMillion;
        if (!qFuzzyCompare(costA + 1.0, costB + 1.0)) {
            return costA < costB;
        }
        return a.id < b.id;
    });

    for (const VexaraCore::OpenRouterModelEntry& entry : sorted) {
        const QString profileId = settings_.models.ensureOpenRouterProfile(
            entry.id, VexaraCore::OpenRouterSettings::displayNameForEntry(entry));
        combo->addItem(VexaraCore::OpenRouterSettings::comboLabelForEntry(entry, roleUse),
                       profileId);
    }

    if (combo->count() == 0) {
        combo->addItem(QStringLiteral("No models — add OPENROUTER_API_KEY and Refresh"));
    }

    combo->setMaxVisibleItems(qMin(30, combo->count()));
    combo->setEnabled(true);

    const QString currentProfileId = settings_.models.modelIdForAgent(agentId);
    if (!currentProfileId.isEmpty()) {
        const int index = combo->findData(currentProfileId);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    }
}

void SettingsDialog::populateModelCombosFromSavedSettings()
{
    const VexaraCore::AgentServiceKind orchestratorBackend =
        backendFromCombo(orchestratorBackendCombo_);
    const VexaraCore::AgentServiceKind builderBackend = backendFromCombo(builderBackendCombo_);
    const VexaraCore::AgentServiceKind supervisorBackend =
        backendFromCombo(supervisorBackendCombo_);

    VexaraCore::OpenClawSettings::ModelCatalogResult quickCatalog;
    QSet<QString> ids;
    for (const QString& preset : VexaraCore::OpenClawSettings::defaultOllamaModelPresets()) {
        ids.insert(preset);
    }
    const QString openClawModel = settings_.agentExecution.openclaw.model.trimmed();
    const QString aiderModel = settings_.agentExecution.aider.model.trimmed();
    if (!openClawModel.isEmpty()) {
        ids.insert(openClawModel);
    }
    if (!aiderModel.isEmpty()) {
        ids.insert(aiderModel);
    }
    quickCatalog.modelIds = QStringList(ids.cbegin(), ids.cend());
    quickCatalog.modelIds.sort(Qt::CaseInsensitive);
    quickCatalog.detail = QStringLiteral("Loading Ollama models…");

    if (modelCatalogStatusLabel_) {
        modelCatalogStatusLabel_->setText(quickCatalog.detail);
    }

    const bool orchEnabled = orchestratorBackend != VexaraCore::AgentServiceKind::None;
    const bool builderEnabled = builderBackend != VexaraCore::AgentServiceKind::None;
    const bool supervisorEnabled = supervisorBackend != VexaraCore::AgentServiceKind::None;

    VexaraCore::OpenRouterPickerCatalog openRouterCatalog;
    if (orchestratorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp
        || supervisorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp
        || builderBackend == VexaraCore::AgentServiceKind::OpenRouterHttp) {
        const VexaraCore::ModelProfile orProfile =
            settings_.models.profileById(QStringLiteral("openrouter-default"));
        openRouterCatalog = VexaraCore::OpenRouterSettings::discoverPickerModels(
            orProfile.resolvedApiKey());
    }

    if (orchestratorBackend == VexaraCore::AgentServiceKind::OpenClawCli) {
        populateCliModelCombo(orchestratorModelCombo_,
                              quickCatalog,
                              openClawModel,
                              orchEnabled);
    } else if (orchestratorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp) {
        populateOpenRouterModelCombo(orchestratorModelCombo_,
                                     openRouterCatalog,
                                     QStringLiteral("orchestrator-1"),
                                     VexaraCore::OpenRouterRoleUse::Planner,
                                     orchEnabled);
    } else if (orchEnabled) {
        populateModelCombo(orchestratorModelCombo_,
                           modelIdsForBackend(orchestratorBackend),
                           settings_.models.modelIdForAgent(QStringLiteral("orchestrator-1")),
                           true);
    }
    if (builderBackend == VexaraCore::AgentServiceKind::AiderCli) {
        populateCliModelCombo(builderModelCombo_, quickCatalog, aiderModel, builderEnabled);
    } else if (builderEnabled) {
        populateModelCombo(builderModelCombo_,
                           modelIdsForBackend(builderBackend),
                           settings_.models.modelIdForAgent(QStringLiteral("builder-1")),
                           true);
    }
    if (supervisorBackend == VexaraCore::AgentServiceKind::OpenClawCli) {
        populateCliModelCombo(supervisorModelCombo_, quickCatalog, openClawModel, supervisorEnabled);
    } else if (supervisorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp) {
        populateOpenRouterModelCombo(supervisorModelCombo_,
                                     openRouterCatalog,
                                     QStringLiteral("supervisor-1"),
                                     VexaraCore::OpenRouterRoleUse::Supervisor,
                                     supervisorEnabled);
    } else if (supervisorEnabled) {
        populateModelCombo(supervisorModelCombo_,
                           modelIdsForBackend(supervisorBackend),
                           settings_.models.modelIdForAgent(QStringLiteral("supervisor-1")),
                           true);
    }

    if (modelCatalogStatusLabel_) {
        if (orchestratorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp
            || supervisorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp) {
            modelCatalogStatusLabel_->setText(
                openRouterCatalog.detail + QStringLiteral("  |  ")
                + VexaraCore::OpenRouterSettings::roleRecommendationHint(
                      VexaraCore::OpenRouterRoleUse::Planner));
        }
    }
}

void SettingsDialog::setModelComboValue(QComboBox* combo,
                                        VexaraCore::AgentServiceKind backendKind,
                                        const QString& agentId,
                                        const QString& cliModelValue) const
{
    if (!combo) {
        return;
    }

    switch (backendKind) {
    case VexaraCore::AgentServiceKind::OpenClawCli:
    case VexaraCore::AgentServiceKind::AiderCli:
        combo->setCurrentText(cliModelValue.trimmed().isEmpty()
                                  ? QStringLiteral("ollama/qwen2.5-coder:14b")
                                  : cliModelValue);
        break;
    case VexaraCore::AgentServiceKind::GrokCli:
    case VexaraCore::AgentServiceKind::OpenAiHttp:
    case VexaraCore::AgentServiceKind::OpenRouterHttp: {
        const QString profileId = settings_.models.modelIdForAgent(agentId);
        const int index = combo->findData(profileId);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
        break;
    }
    case VexaraCore::AgentServiceKind::None:
        break;
    }
}

QString SettingsDialog::modelValueFromCombo(const QComboBox* combo,
                                            VexaraCore::AgentServiceKind backendKind) const
{
    if (!combo) {
        return QString();
    }
    switch (backendKind) {
    case VexaraCore::AgentServiceKind::OpenClawCli:
    case VexaraCore::AgentServiceKind::AiderCli: {
        const QString data = combo->currentData(Qt::UserRole).toString().trimmed();
        if (!data.isEmpty()) {
            return data;
        }
        const QString text = combo->currentText().trimmed();
        if (text.startsWith(QStringLiteral("──")) || text.contains(QStringLiteral("click Refresh"))) {
            return QString();
        }
        return text;
    }
    case VexaraCore::AgentServiceKind::GrokCli:
    case VexaraCore::AgentServiceKind::OpenAiHttp:
    case VexaraCore::AgentServiceKind::OpenRouterHttp:
        return combo->currentData().toString();
    case VexaraCore::AgentServiceKind::None:
        break;
    }
    return QString();
}

void SettingsDialog::refreshRoleModelCombos(bool queryOpenClawCli)
{
    const VexaraCore::AgentServiceKind orchestratorBackend =
        backendFromCombo(orchestratorBackendCombo_);
    const VexaraCore::AgentServiceKind builderBackend = backendFromCombo(builderBackendCombo_);
    const VexaraCore::AgentServiceKind supervisorBackend =
        backendFromCombo(supervisorBackendCombo_);

    VexaraCore::OpenClawSettings::ModelCatalogResult openClawCatalog;
    const bool needsOpenClawCatalog =
        orchestratorBackend == VexaraCore::AgentServiceKind::OpenClawCli
        || supervisorBackend == VexaraCore::AgentServiceKind::OpenClawCli;
    if (needsOpenClawCatalog) {
        VexaraCore::OpenClawSettings openClaw = settings_.agentExecution.openclaw;
        if (openClawCommandEdit_) {
            openClaw.command = openClawCommandEdit_->text().trimmed();
        }
        openClawCatalog = openClaw.discoverOpenClawPickerModels(queryOpenClawCli);
    }

    VexaraCore::OpenClawSettings::ModelCatalogResult aiderCatalog;
    if (builderBackend == VexaraCore::AgentServiceKind::AiderCli) {
        aiderCatalog = VexaraCore::OpenClawSettings::discoverOllamaPickerModels(
            settings_.agentExecution.openclaw.ollamaBaseUrl);
    }

    VexaraCore::OpenRouterPickerCatalog openRouterCatalog;
    const bool needsOpenRouter =
        orchestratorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp
        || supervisorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp
        || builderBackend == VexaraCore::AgentServiceKind::OpenRouterHttp;
    if (needsOpenRouter) {
        QString apiKey = apiKeyFromField(QStringLiteral("openrouter-default"));
        if (apiKey.isEmpty()) {
            const VexaraCore::ModelProfile orProfile =
                settings_.models.profileById(QStringLiteral("openrouter-default"));
            apiKey = orProfile.resolvedApiKey();
        }
        openRouterCatalog = VexaraCore::OpenRouterSettings::discoverPickerModels(apiKey);
    }

    if (modelCatalogStatusLabel_) {
        QStringList notes;
        if (orchestratorBackend == VexaraCore::AgentServiceKind::OpenClawCli) {
            notes.append(QStringLiteral("Orchestrator: %1").arg(openClawCatalog.detail));
        } else if (orchestratorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp) {
            notes.append(QStringLiteral("Orchestrator: %1").arg(openRouterCatalog.detail));
        } else if (orchestratorBackend != VexaraCore::AgentServiceKind::None) {
            notes.append(QStringLiteral("Orchestrator: %1 profile(s)")
                             .arg(modelIdsForBackend(orchestratorBackend).size()));
        }
        if (builderBackend == VexaraCore::AgentServiceKind::AiderCli) {
            notes.append(QStringLiteral("Builder: %1").arg(aiderCatalog.detail));
        } else if (builderBackend != VexaraCore::AgentServiceKind::None) {
            notes.append(QStringLiteral("Builder: %1 profile(s)")
                             .arg(modelIdsForBackend(builderBackend).size()));
        }
        if (supervisorBackend == VexaraCore::AgentServiceKind::OpenClawCli) {
            notes.append(QStringLiteral("Supervisor: %1").arg(openClawCatalog.detail));
        } else if (supervisorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp) {
            notes.append(QStringLiteral("Supervisor: %1").arg(openRouterCatalog.detail));
        } else if (supervisorBackend != VexaraCore::AgentServiceKind::None) {
            notes.append(QStringLiteral("Supervisor: %1 profile(s)")
                             .arg(modelIdsForBackend(supervisorBackend).size()));
        }
        if (needsOpenRouter) {
            notes.append(VexaraCore::OpenRouterSettings::roleRecommendationHint(
                VexaraCore::OpenRouterRoleUse::Planner));
        }
        modelCatalogStatusLabel_->setText(notes.join(QStringLiteral("  |  ")));
    }

    const auto selectedFor = [this](const QString& agentId,
                                    VexaraCore::AgentServiceKind backend) -> QString {
        switch (backend) {
        case VexaraCore::AgentServiceKind::OpenClawCli:
            return settings_.agentExecution.openclaw.model;
        case VexaraCore::AgentServiceKind::AiderCli:
            return settings_.agentExecution.aider.model;
        case VexaraCore::AgentServiceKind::GrokCli:
        case VexaraCore::AgentServiceKind::OpenAiHttp:
        case VexaraCore::AgentServiceKind::OpenRouterHttp:
            return settings_.models.modelIdForAgent(agentId);
        case VexaraCore::AgentServiceKind::None:
            break;
        }
        return QString();
    };

    const bool orchEnabled = orchestratorBackend != VexaraCore::AgentServiceKind::None;
    const bool builderEnabled = builderBackend != VexaraCore::AgentServiceKind::None;
    const bool supervisorEnabled = supervisorBackend != VexaraCore::AgentServiceKind::None;

    if (orchestratorBackend == VexaraCore::AgentServiceKind::OpenClawCli) {
        populateCliModelCombo(orchestratorModelCombo_,
                              openClawCatalog,
                              selectedFor(QStringLiteral("orchestrator-1"), orchestratorBackend),
                              orchEnabled);
    } else if (orchestratorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp) {
        populateOpenRouterModelCombo(orchestratorModelCombo_,
                                     openRouterCatalog,
                                     QStringLiteral("orchestrator-1"),
                                     VexaraCore::OpenRouterRoleUse::Planner,
                                     orchEnabled);
    } else {
        populateModelCombo(orchestratorModelCombo_,
                           modelIdsForBackend(orchestratorBackend),
                           selectedFor(QStringLiteral("orchestrator-1"), orchestratorBackend),
                           orchEnabled);
    }

    if (builderBackend == VexaraCore::AgentServiceKind::AiderCli) {
        populateCliModelCombo(builderModelCombo_,
                              aiderCatalog,
                              selectedFor(QStringLiteral("builder-1"), builderBackend),
                              builderEnabled);
    } else {
        populateModelCombo(builderModelCombo_,
                           modelIdsForBackend(builderBackend),
                           selectedFor(QStringLiteral("builder-1"), builderBackend),
                           builderEnabled);
    }

    if (supervisorBackend == VexaraCore::AgentServiceKind::OpenClawCli) {
        populateCliModelCombo(supervisorModelCombo_,
                              openClawCatalog,
                              selectedFor(QStringLiteral("supervisor-1"), supervisorBackend),
                              supervisorEnabled);
    } else if (supervisorBackend == VexaraCore::AgentServiceKind::OpenRouterHttp) {
        populateOpenRouterModelCombo(supervisorModelCombo_,
                                     openRouterCatalog,
                                     QStringLiteral("supervisor-1"),
                                     VexaraCore::OpenRouterRoleUse::Supervisor,
                                     supervisorEnabled);
    } else {
        populateModelCombo(supervisorModelCombo_,
                           modelIdsForBackend(supervisorBackend),
                           selectedFor(QStringLiteral("supervisor-1"), supervisorBackend),
                           supervisorEnabled);
    }

    if (orchestratorBackend == VexaraCore::AgentServiceKind::GrokCli
        || orchestratorBackend == VexaraCore::AgentServiceKind::OpenAiHttp) {
        setModelComboValue(orchestratorModelCombo_,
                           orchestratorBackend,
                           QStringLiteral("orchestrator-1"),
                           QString());
    }
    if (builderBackend == VexaraCore::AgentServiceKind::GrokCli
        || builderBackend == VexaraCore::AgentServiceKind::OpenAiHttp) {
        setModelComboValue(builderModelCombo_,
                           builderBackend,
                           QStringLiteral("builder-1"),
                           QString());
    }
    if (supervisorBackend == VexaraCore::AgentServiceKind::GrokCli
        || supervisorBackend == VexaraCore::AgentServiceKind::OpenAiHttp) {
        setModelComboValue(supervisorModelCombo_,
                           supervisorBackend,
                           QStringLiteral("supervisor-1"),
                           QString());
    }
}

} // namespace VexaraEditor
