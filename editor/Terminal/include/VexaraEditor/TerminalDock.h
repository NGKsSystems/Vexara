#pragma once

#include "VexaraCore/TerminalSettings.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QToolButton;

namespace VexaraEditor {

class TerminalPanel;

class TerminalDock : public QWidget {
    Q_OBJECT

public:
    explicit TerminalDock(VexaraCore::TerminalSettings& settings, QWidget* parent = nullptr);

    TerminalPanel* panel() const;
    void setWorkingDirectory(const QString& path);
    void rebuildProfileMenu();

signals:
    void defaultProfileChanged(const QString& profileId);

private:
    void refreshProfileSelector();
    void onProfileSelected(int index);
    void newTerminalWithProfile(const QString& profileId);
    void setDefaultProfile(const QString& profileId);

    VexaraCore::TerminalSettings& settings_;
    TerminalPanel* panel_ = nullptr;
    QComboBox* profileCombo_ = nullptr;
    QToolButton* newButton_ = nullptr;
    QLabel* profileLabel_ = nullptr;
    bool suppressProfileSignal_ = false;
};

} // namespace VexaraEditor
