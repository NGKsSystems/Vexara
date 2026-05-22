#include "App.h"

#include "VexaraCore/AppIdentity.h"
#include "VexaraEditor/MainWindow.h"
#include "VexaraOrchestration/Orchestrator.h"

#include <QApplication>
#include <QCoreApplication>

namespace vexara::app {

void MainWindowDeleter::operator()(VexaraEditor::MainWindow* window) noexcept
{
    delete window;
}

App::App() = default;
App::~App() = default;

int App::run(int argc, char* argv[])
{
    QApplication qtApp(argc, argv);
    QCoreApplication::setOrganizationName(VexaraCore::AppIdentity::organizationName());
    QCoreApplication::setApplicationName(VexaraCore::AppIdentity::applicationName());

    orchestrator_ = std::make_unique<VexaraOrchestration::Orchestrator>();
    orchestrator_->bootstrapDefaultAgents();

    mainWindow_.reset(new VexaraEditor::MainWindow(*orchestrator_));
    mainWindow_->show();

    return qtApp.exec();
}

} // namespace vexara::app
