#pragma once

#include <QDockWidget>
#include <QMainWindow>

#include "VexaraCore/GlobalSettings.h"
#include "VexaraOrchestration/Orchestrator.h"

namespace VexaraEditor {

class AgentsPanel;
class DocumentWorkspace;
class FindBar;
class ProjectTreePanel;
class TerminalDock;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(VexaraOrchestration::Orchestrator& orchestrator, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void buildMenuBar();
    void wireSignals();
    void restoreLastProject();
    void setProjectRoot(const QString& path);
    void openFolderDialog();
    void openFileDialog();
    void refreshWindowTitle();

    VexaraOrchestration::Orchestrator& orchestrator_;
    VexaraCore::GlobalSettings globalSettings_;
    QString projectRoot_;

    ProjectTreePanel* explorer_ = nullptr;
    DocumentWorkspace* workspace_ = nullptr;
    AgentsPanel* agentsPanel_ = nullptr;
    FindBar* findBar_ = nullptr;
    TerminalDock* terminalDockWidget_ = nullptr;
    QDockWidget* terminalDock_ = nullptr;
};

} // namespace VexaraEditor
