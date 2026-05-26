#pragma once

#include "VexaraCore/GlobalSettings.h"
#include "VexaraCore/OpenRouterSettings.h"

#include <QDialog>
#include <QHash>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace VexaraEditor {

class ApiKeyValidator;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(VexaraCore::GlobalSettings& settings, QWidget* parent = nullptr);
    ~SettingsDialog() override;

private:
    void loadFields();
    void applyFields();
    void browseGrokCommand();
    void browseOpenClawCommand();
    void configureOpenClawForOllama();
    void browseAiderCommand();
    void validateAllApiKeys();
    void validateNextApiKey();
    void setKeyStatus(const QString& profileId, const QString& text, bool successHint);
    VexaraCore::ModelProfile profileForId(const QString& profileId) const;
    QString apiKeyFromField(const QString& profileId) const;
    void refreshKeyFieldUi(const QString& profileId);
    void removeStoredApiKey(const QString& profileId);
    void populateBackendCombo(QComboBox* combo) const;
    VexaraCore::AgentServiceKind backendFromCombo(const QComboBox* combo) const;
    void setBackendCombo(QComboBox* combo, VexaraCore::AgentServiceKind kind) const;
    void populateModelCombo(QComboBox* combo,
                            const QStringList& modelIds,
                            const QString& selectedModel,
                            bool enabled,
                            const QString& emptyHint = QString()) const;
    void populateCliModelCombo(QComboBox* combo,
                               const VexaraCore::OpenClawSettings::ModelCatalogResult& catalog,
                               const QString& selectedModel,
                               bool enabled) const;
    void populateOpenRouterModelCombo(QComboBox* combo,
                                      const VexaraCore::OpenRouterPickerCatalog& catalog,
                                      const QString& agentId,
                                      VexaraCore::OpenRouterRoleUse roleUse,
                                      bool enabled);
    QStringList modelIdsForBackend(VexaraCore::AgentServiceKind backendKind) const;
    void setModelComboValue(QComboBox* combo,
                            VexaraCore::AgentServiceKind backendKind,
                            const QString& agentId,
                            const QString& cliModelValue) const;
    QString modelValueFromCombo(const QComboBox* combo,
                                VexaraCore::AgentServiceKind backendKind) const;
    void populateModelCombosFromSavedSettings();
    void refreshRoleModelCombos(bool queryOpenClawCli = false);

    VexaraCore::GlobalSettings& settings_;
    ApiKeyValidator* keyValidator_ = nullptr;
    QLabel* configPathLabel_ = nullptr;
    QLineEdit* grokCommandEdit_ = nullptr;
    QPushButton* grokBrowseButton_ = nullptr;
    QLabel* grokPathStatus_ = nullptr;
    QLineEdit* grokArgsEdit_ = nullptr;
    QLineEdit* openClawCommandEdit_ = nullptr;
    QPushButton* openClawBrowseButton_ = nullptr;
    QLabel* openClawPathStatus_ = nullptr;
    QLineEdit* openClawAgentIdEdit_ = nullptr;
    QComboBox* orchestratorModelCombo_ = nullptr;
    QComboBox* builderModelCombo_ = nullptr;
    QComboBox* supervisorModelCombo_ = nullptr;
    QPushButton* refreshModelsButton_ = nullptr;
    QLabel* modelCatalogStatusLabel_ = nullptr;
    QLineEdit* openClawArgsEdit_ = nullptr;
    QPushButton* openClawConfigureOllamaButton_ = nullptr;
    QLineEdit* aiderCommandEdit_ = nullptr;
    QPushButton* aiderBrowseButton_ = nullptr;
    QLabel* aiderPathStatus_ = nullptr;
    QLineEdit* aiderArgsEdit_ = nullptr;
    QFormLayout* apiKeyForm_ = nullptr;
    QHash<QString, QLineEdit*> apiKeyEdits_;
    QHash<QString, QLabel*> apiKeyStatus_;
    QHash<QString, QPushButton*> apiKeyRemoveButtons_;
    QPushButton* validateKeysButton_ = nullptr;
    QStringList validationQueue_;
    QLineEdit* verifyCommandEdit_ = nullptr;
    QComboBox* orchestratorBackendCombo_ = nullptr;
    QComboBox* builderBackendCombo_ = nullptr;
    QComboBox* supervisorBackendCombo_ = nullptr;
};

} // namespace VexaraEditor
