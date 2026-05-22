#include "VexaraEditor/SettingsDialog.h"

#include "VexaraCore/ModelProfile.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace VexaraEditor {

SettingsDialog::SettingsDialog(VexaraCore::GlobalSettings& settings, QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
{
    setWindowTitle(QStringLiteral("Vexara Settings"));
    resize(520, 420);

    settings_.models.ensureDefaults();

    auto* modelsGroup = new QGroupBox(QStringLiteral("Models (read-only overview)"), this);
    modelsList_ = new QListWidget(modelsGroup);
    auto* modelsLayout = new QVBoxLayout(modelsGroup);
    modelsLayout->addWidget(modelsList_);
    modelsLayout->addWidget(new QLabel(
        QStringLiteral("Edit profiles and API keys in vexara.json (models section)."), modelsGroup));

    auto* grokGroup = new QGroupBox(QStringLiteral("Grok Build"), this);
    grokCommandEdit_ = new QLineEdit(grokGroup);
    grokCommandEdit_->setPlaceholderText(QStringLiteral("Path to Grok Build CLI executable"));
    grokArgsEdit_ = new QLineEdit(grokGroup);
    grokArgsEdit_->setPlaceholderText(QStringLiteral("Args separated by spaces; use {prompt} and {cwd}"));
    auto* grokForm = new QFormLayout(grokGroup);
    grokForm->addRow(QStringLiteral("Command:"), grokCommandEdit_);
    grokForm->addRow(QStringLiteral("Arguments:"), grokArgsEdit_);

    auto* verifyGroup = new QGroupBox(QStringLiteral("Verification"), this);
    verifyCommandEdit_ = new QLineEdit(verifyGroup);
    verifyCommandEdit_->setPlaceholderText(QStringLiteral("Command run via cmd /C in project folder"));
    auto* verifyForm = new QFormLayout(verifyGroup);
    verifyForm->addRow(QStringLiteral("Command:"), verifyCommandEdit_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyFields();
        settings_.save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(modelsGroup);
    layout->addWidget(grokGroup);
    layout->addWidget(verifyGroup);
    layout->addWidget(buttons);

    loadFields();
}

void SettingsDialog::loadFields()
{
    modelsList_->clear();
    for (const VexaraCore::ModelProfile& profile : settings_.models.profiles()) {
        const QString line = QStringLiteral("%1 - %2 (%3)")
                                 .arg(profile.displayName,
                                      profile.modelName,
                                      VexaraCore::modelProviderLabel(profile.provider));
        modelsList_->addItem(line);
    }

    grokCommandEdit_->setText(settings_.grokBuild.command);
    grokArgsEdit_->setText(settings_.grokBuild.args.join(QStringLiteral(" ")));
    verifyCommandEdit_->setText(settings_.verification.command);
}

void SettingsDialog::applyFields()
{
    settings_.grokBuild.command = grokCommandEdit_->text().trimmed();
    settings_.grokBuild.args = grokArgsEdit_->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    settings_.verification.command = verifyCommandEdit_->text().trimmed();
}

} // namespace VexaraEditor
