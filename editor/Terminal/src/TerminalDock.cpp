#include "VexaraEditor/TerminalDock.h"

#include "VexaraEditor/TerminalPanel.h"

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

namespace VexaraEditor {

TerminalDock::TerminalDock(VexaraCore::TerminalSettings& settings, QWidget* parent)
    : QWidget(parent)
    , settings_(settings)
{
    settings_.ensureDefaults();

    auto* toolbar = new QWidget(this);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 4, 4, 0);
    toolbarLayout->setSpacing(8);

    profileLabel_ = new QLabel(QStringLiteral("Shell:"), toolbar);

    profileCombo_ = new QComboBox(toolbar);
    profileCombo_->setMinimumWidth(200);
    profileCombo_->setToolTip(QStringLiteral("Choose terminal shell (Command Prompt, PowerShell, Git Bash, etc.)"));

    newButton_ = new QToolButton(toolbar);
    newButton_->setText(QStringLiteral("New"));
    newButton_->setToolTip(QStringLiteral("Restart terminal with selected shell"));
    newButton_->setPopupMode(QToolButton::InstantPopup);

    toolbarLayout->addWidget(profileLabel_);
    toolbarLayout->addWidget(profileCombo_, 1);
    toolbarLayout->addWidget(newButton_);

    panel_ = new TerminalPanel(this);
    refreshProfileSelector();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(panel_, 1);

    rebuildProfileMenu();

    connect(profileCombo_, QOverload<int>::of(&QComboBox::activated), this,
            &TerminalDock::onProfileSelected);
    connect(newButton_, &QToolButton::clicked, this, [this]() {
        onProfileSelected(profileCombo_->currentIndex());
    });
    connect(panel_, &TerminalPanel::profileChanged, this, [this](const QString& profileId) {
        suppressProfileSignal_ = true;
        for (int i = 0; i < profileCombo_->count(); ++i) {
            if (profileCombo_->itemData(i).toString() == profileId) {
                profileCombo_->setCurrentIndex(i);
                break;
            }
        }
        suppressProfileSignal_ = false;
        profileLabel_->setText(
            QStringLiteral("Shell: %1").arg(settings_.profileById(profileId).displayName));
    });
}

TerminalPanel* TerminalDock::panel() const
{
    return panel_;
}

void TerminalDock::setWorkingDirectory(const QString& path)
{
    panel_->setWorkingDirectory(path);
}

void TerminalDock::refreshProfileSelector()
{
    suppressProfileSignal_ = true;
    profileCombo_->clear();

    const QVector<VexaraCore::TerminalProfile> profiles = settings_.profiles();
    int selectIndex = 0;
    for (int i = 0; i < profiles.size(); ++i) {
        const VexaraCore::TerminalProfile& profile = profiles[i];
        profileCombo_->addItem(profile.displayName, profile.id);
        if (profile.id == settings_.defaultProfileId) {
            selectIndex = i;
        }
    }

    if (profileCombo_->count() == 0) {
        settings_.ensureDefaults();
        return refreshProfileSelector();
    }

    profileCombo_->setCurrentIndex(selectIndex);
    const VexaraCore::TerminalProfile initial =
        settings_.profileById(profileCombo_->currentData().toString());
    panel_->setProfile(initial);
    suppressProfileSignal_ = false;
}

void TerminalDock::onProfileSelected(int index)
{
    if (suppressProfileSignal_ || index < 0 || index >= profileCombo_->count()) {
        return;
    }
    const QString profileId = profileCombo_->itemData(index).toString();
    newTerminalWithProfile(profileId);
}

void TerminalDock::rebuildProfileMenu()
{
    auto* menu = new QMenu(newButton_);
    menu->addAction(QStringLiteral("Restart Terminal"), this, [this]() {
        onProfileSelected(profileCombo_->currentIndex());
    });
    menu->addSeparator();

    const QVector<VexaraCore::TerminalProfile> profiles = settings_.profiles();
    for (const VexaraCore::TerminalProfile& profile : profiles) {
        menu->addAction(profile.displayName, this, [this, id = profile.id]() {
            for (int i = 0; i < profileCombo_->count(); ++i) {
                if (profileCombo_->itemData(i).toString() == id) {
                    profileCombo_->setCurrentIndex(i);
                    onProfileSelected(i);
                    break;
                }
            }
        });
    }

    menu->addSeparator();
    auto* defaultMenu = menu->addMenu(QStringLiteral("Set Default Shell"));
    for (const VexaraCore::TerminalProfile& profile : profiles) {
        QAction* action = defaultMenu->addAction(profile.displayName);
        action->setCheckable(true);
        action->setChecked(profile.id == settings_.defaultProfileId);
        connect(action, &QAction::triggered, this, [this, id = profile.id]() {
            setDefaultProfile(id);
        });
    }

    newButton_->setMenu(menu);
}

void TerminalDock::newTerminalWithProfile(const QString& profileId)
{
    if (!settings_.hasProfile(profileId)) {
        settings_.ensureDefaults();
        refreshProfileSelector();
        return;
    }
    panel_->setProfile(settings_.profileById(profileId));
    panel_->restartShell();
}

void TerminalDock::setDefaultProfile(const QString& profileId)
{
    if (!settings_.hasProfile(profileId)) {
        return;
    }
    settings_.setDefaultProfileId(profileId);
    emit defaultProfileChanged(profileId);
    rebuildProfileMenu();
}

} // namespace VexaraEditor
