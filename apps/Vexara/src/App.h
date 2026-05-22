#pragma once

#include <memory>

namespace VexaraOrchestration {
class Orchestrator;
}

namespace VexaraEditor {
class MainWindow;
}

namespace vexara::app {

struct MainWindowDeleter {
    void operator()(VexaraEditor::MainWindow* window) noexcept;
};

class App {
public:
    App();
    ~App();

    int run(int argc, char* argv[]);

private:
    std::unique_ptr<VexaraOrchestration::Orchestrator> orchestrator_;
    std::unique_ptr<VexaraEditor::MainWindow, MainWindowDeleter> mainWindow_;
};

} // namespace vexara::app
