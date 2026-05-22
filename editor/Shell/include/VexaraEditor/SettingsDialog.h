#pragma once

#include "VexaraCore/GlobalSettings.h"

#include <QDialog>

class QLineEdit;
class QListWidget;

namespace VexaraEditor {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(VexaraCore::GlobalSettings& settings, QWidget* parent = nullptr);

private:
    void loadFields();
    void applyFields();

    VexaraCore::GlobalSettings& settings_;
    QListWidget* modelsList_ = nullptr;
    QLineEdit* grokCommandEdit_ = nullptr;
    QLineEdit* grokArgsEdit_ = nullptr;
    QLineEdit* verifyCommandEdit_ = nullptr;
};

} // namespace VexaraEditor
