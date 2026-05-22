#include "VexaraEditor/MainWindow.h"

#include "VexaraEditor/AgentsPanel.h"
#include "VexaraEditor/DocumentWorkspace.h"
#include "VexaraEditor/FindBar.h"
#include "VexaraEditor/ProjectTreePanel.h"
#include "VexaraEditor/TerminalDock.h"
#include "VexaraEditor/TerminalPanel.h"
#include "VexaraEditor/SettingsDialog.h"
#include "VexaraEditor/TextContextMenu.h"
#include "VexaraCore/AppIdentity.h"
#include "VexaraCore/ProjectSettings.h"

#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QApplication>
#include <QToolButton>
#include <QVBoxLayout>

#include "VexaraOrchestration/Orchestrator.h"

namespace VexaraEditor {

MainWindow::MainWindow(VexaraOrchestration::Orchestrator& orchestrator, QWidget* parent)
    : QMainWindow(parent)
    , orchestrator_(orchestrator)
{
    const QString logoPath = QCoreApplication::applicationDirPath()
                             + QStringLiteral("/assets/Vexara Logo.jpg");
    if (QFileInfo::exists(logoPath)) {
        setWindowIcon(QIcon(logoPath));
    }
    resize(1280, 800);

    globalSettings_.load();
    globalSettings_.terminal.ensureDefaults();
    orchestrator_.configure(globalSettings_);
    buildUi();
    buildMenuBar();
    wireSignals();
    refreshWindowTitle();
    restoreLastProject();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!projectRoot_.isEmpty()) {
        globalSettings_.lastProjectRoot = projectRoot_;
    }
    globalSettings_.save();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    explorer_ = new ProjectTreePanel(this);
    workspace_ = new DocumentWorkspace(this);
    agentsPanel_ = new AgentsPanel(this);
    editorFindBar_ = new FindBar(this);
    terminalDockWidget_ = new TerminalDock(globalSettings_.terminal, this);
    agentsPanel_->bindOrchestrator(orchestrator_);

    editorFindStrip_ = new QWidget(this);
    editorFindStrip_->setVisible(false);
    auto* editorFindLayout = new QVBoxLayout(editorFindStrip_);
    editorFindLayout->setContentsMargins(0, 0, 0, 0);
    editorFindLayout->setSpacing(0);
    editorFindLayout->addWidget(editorFindBar_);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(editorFindStrip_);
    centralLayout->addWidget(workspace_, 1);
    setCentralWidget(central);

    explorerDock_ = new QDockWidget(QStringLiteral("Explorer"), this);
    explorerDock_->setObjectName(QStringLiteral("dock_project"));
    explorerDock_->setWidget(explorer_);
    explorerDock_->setMinimumWidth(280);
    addDockWidget(Qt::LeftDockWidgetArea, explorerDock_);
    resizeDocks({explorerDock_}, {360}, Qt::Horizontal);

    auto* agentsDock = new QDockWidget(QStringLiteral("Agents"), this);
    agentsDock->setObjectName(QStringLiteral("dock_agents"));
    agentsDock->setWidget(agentsPanel_);
    addDockWidget(Qt::RightDockWidgetArea, agentsDock);

    terminalDock_ = new QDockWidget(QStringLiteral("Terminal"), this);
    terminalDock_->setObjectName(QStringLiteral("dock_terminal"));
    terminalDock_->setWidget(terminalDockWidget_);
    addDockWidget(Qt::BottomDockWidgetArea, terminalDock_);

    auto* toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);
    auto* openButton = new QToolButton(toolbar);
    openButton->setText(QStringLiteral("Open"));
    openButton->setPopupMode(QToolButton::InstantPopup);
    auto* openMenu = new QMenu(openButton);
    openMenu->addAction(QStringLiteral("Open Folder..."), this, &MainWindow::openFolderDialog);
    openMenu->addAction(QStringLiteral("Open File..."), this, &MainWindow::openFileDialog);
    openButton->setMenu(openMenu);
    toolbar->addWidget(openButton);

    statusBar()->showMessage(QStringLiteral("Use File > Open Folder to open your project."));
}

void MainWindow::buildMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    QAction* openFolderAction = fileMenu->addAction(QStringLiteral("Open &Folder..."));
    openFolderAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+K Ctrl+O")));
    QAction* openFileAction = fileMenu->addAction(QStringLiteral("Open &File..."));
    openFileAction->setShortcut(QKeySequence::Open);

    fileMenu->addSeparator();

    QAction* saveAction = fileMenu->addAction(QStringLiteral("&Save"));
    saveAction->setShortcut(QKeySequence::Save);
    QAction* saveAllAction = fileMenu->addAction(QStringLiteral("Save A&ll"));
    saveAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    QAction* cutAction = editMenu->addAction(QStringLiteral("Cu&t"));
    cutAction->setShortcut(QKeySequence::Cut);
    QAction* copyAction = editMenu->addAction(QStringLiteral("&Copy"));
    copyAction->setShortcut(QKeySequence::Copy);
    QAction* pasteAction = editMenu->addAction(QStringLiteral("&Paste"));
    pasteAction->setShortcut(QKeySequence::Paste);
    QAction* selectAllAction = editMenu->addAction(QStringLiteral("Select &All"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    editMenu->addSeparator();
    QAction* findAction = editMenu->addAction(QStringLiteral("&Find in Editor"));
    findAction->setShortcut(QKeySequence::Find);
    QAction* findInTreeAction = editMenu->addAction(QStringLiteral("Find in &Files"));
    findInTreeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    QAction* replaceAction = editMenu->addAction(QStringLiteral("&Replace"));
    replaceAction->setShortcut(QKeySequence::Replace);

    QMenu* settingsMenu = menuBar()->addMenu(QStringLiteral("&Settings"));
    QAction* preferencesAction = settingsMenu->addAction(QStringLiteral("&Preferences..."));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    QAction* terminalAction = viewMenu->addAction(QStringLiteral("&Terminal"));
    terminalAction->setCheckable(true);
    terminalAction->setChecked(true);
    terminalAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+`")));

    connect(preferencesAction, &QAction::triggered, this, [this]() {
        SettingsDialog dialog(globalSettings_, this);
        if (dialog.exec() == QDialog::Accepted) {
            orchestrator_.configure(globalSettings_);
            statusBar()->showMessage(QStringLiteral("Settings saved"));
        }
    });

    connect(openFolderAction, &QAction::triggered, this, &MainWindow::openFolderDialog);
    connect(openFileAction, &QAction::triggered, this, &MainWindow::openFileDialog);

    connect(saveAction, &QAction::triggered, this, [this]() {
        if (workspace_->saveActiveFile()) {
            statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(workspace_->activeFilePath()).fileName()));
        } else {
            statusBar()->showMessage(QStringLiteral("Nothing to save"));
        }
    });

    connect(saveAllAction, &QAction::triggered, this, [this]() {
        const int saved = workspace_->saveAllOpenFiles();
        statusBar()->showMessage(QStringLiteral("Saved %1 file(s)").arg(saved));
    });

    connect(cutAction, &QAction::triggered, this, []() { dispatchCutToFocusWidget(); });
    connect(copyAction, &QAction::triggered, this, []() { dispatchCopyToFocusWidget(); });
    connect(pasteAction, &QAction::triggered, this, [this]() {
        QWidget* focused = QApplication::focusWidget();
        if (terminalDockWidget_->panel()->containsFocusWidget(focused)) {
            terminalDockWidget_->panel()->pasteFromClipboard();
            return;
        }
        dispatchPasteToFocusWidget();
    });
    connect(selectAllAction, &QAction::triggered, this, []() { dispatchSelectAllToFocusWidget(); });

    connect(findAction, &QAction::triggered, this, [this]() {
        if (editorFindStrip_->isVisible()) {
            hideEditorFind();
        } else {
            showEditorFind(false);
        }
    });

    connect(findInTreeAction, &QAction::triggered, this, [this]() {
        if (explorer_->isFileFilterVisible()) {
            hideTreeFind();
        } else {
            showTreeFind();
        }
    });

    connect(replaceAction, &QAction::triggered, this, [this]() {
        if (editorFindStrip_->isVisible()) {
            hideEditorFind();
        } else {
            showEditorFind(true);
        }
    });

    connect(terminalAction, &QAction::toggled, this, [this](bool visible) {
        terminalDock_->setVisible(visible);
        if (visible) {
            terminalDock_->raise();
            terminalDockWidget_->panel()->focusCommandLine();
        }
    });

    connect(terminalDockWidget_, &TerminalDock::defaultProfileChanged, this, [this]() {
        globalSettings_.save();
    });

    connect(&orchestrator_, &VexaraOrchestration::Orchestrator::taskStateChanged, this,
            [this](const QString& summary) {
                statusBar()->showMessage(summary.left(200));
                agentsPanel_->refresh();
            });
    connect(&orchestrator_, &VexaraOrchestration::Orchestrator::verificationStateChanged, this,
            [this](const QString& summary) {
                statusBar()->showMessage(summary.left(200));
                agentsPanel_->refresh();
            });
}

void MainWindow::wireSignals()
{
    connect(explorer_, &ProjectTreePanel::fileActivated, this, [this](const QString& path) {
        const QFileInfo info(path);
        if (!info.isFile()) {
            return;
        }
        if (workspace_->openFile(path)) {
            statusBar()->showMessage(QStringLiteral("Opened %1").arg(info.fileName()));
        } else if (workspace_->isTabLimitReached()) {
            statusBar()->showMessage(
                QStringLiteral("Tab limit reached (%1). Close a tab before opening another file.")
                    .arg(workspace_->maxOpenTabs()));
        }
    });

    connect(editorFindBar_, &FindBar::findNextRequested, this, [this]() {
        if (workspace_->findInActiveDocument(editorFindBar_->query(), true)) {
            statusBar()->showMessage(QStringLiteral("Match found"));
        } else {
            statusBar()->showMessage(QStringLiteral("No match found"));
        }
    });

    connect(editorFindBar_, &FindBar::findPreviousRequested, this, [this]() {
        if (workspace_->findInActiveDocument(editorFindBar_->query(), false)) {
            statusBar()->showMessage(QStringLiteral("Match found"));
        } else {
            statusBar()->showMessage(QStringLiteral("No match found"));
        }
    });

    connect(editorFindBar_, &FindBar::replaceRequested, this, [this]() {
        if (editorFindBar_->query().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Enter text to find"));
            return;
        }
        if (workspace_->replaceInActiveDocument(editorFindBar_->query(), editorFindBar_->replacement(), false)) {
            statusBar()->showMessage(QStringLiteral("Replaced"));
        } else {
            statusBar()->showMessage(QStringLiteral("No match to replace"));
        }
    });

    connect(editorFindBar_, &FindBar::replaceAllRequested, this, [this]() {
        if (editorFindBar_->query().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Enter text to find"));
            return;
        }
        if (workspace_->replaceInActiveDocument(editorFindBar_->query(), editorFindBar_->replacement(), true)) {
            statusBar()->showMessage(QStringLiteral("Replaced all matches"));
        } else {
            statusBar()->showMessage(QStringLiteral("No matches to replace"));
        }
    });
}

void MainWindow::showEditorFind(bool focusReplace)
{
    hideTreeFind();
    editorFindStrip_->setVisible(true);
    if (focusReplace) {
        editorFindBar_->focusReplace();
    } else {
        editorFindBar_->focusQuery();
    }
}

void MainWindow::hideEditorFind()
{
    editorFindStrip_->setVisible(false);
}

void MainWindow::showTreeFind()
{
    hideEditorFind();
    explorerDock_->show();
    explorerDock_->raise();
    explorer_->showFileFilter();
}

void MainWindow::hideTreeFind()
{
    explorer_->hideFileFilter();
}

void MainWindow::openFolderDialog()
{
    const QString startDir = projectRoot_.isEmpty() ? globalSettings_.lastProjectRoot : projectRoot_;
    const QString chosen = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Open Folder"),
        startDir.isEmpty() ? QDir::homePath() : startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (chosen.isEmpty()) {
        return;
    }
    setProjectRoot(chosen);
}

void MainWindow::openFileDialog()
{
    const QString startDir = projectRoot_.isEmpty() ? QDir::homePath() : projectRoot_;
    const QString chosen = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open File"),
        startDir,
        QStringLiteral("All Files (*);;Text Files (*.txt *.md *.json *.cpp *.h *.hpp *.c *.py *.js *.ts *.toml *.xml)"));

    if (chosen.isEmpty()) {
        return;
    }

    if (workspace_->openFile(chosen)) {
        statusBar()->showMessage(QStringLiteral("Opened %1").arg(QFileInfo(chosen).fileName()));
    } else if (workspace_->isTabLimitReached()) {
        statusBar()->showMessage(
            QStringLiteral("Tab limit reached (%1). Close a tab before opening another file.")
                .arg(workspace_->maxOpenTabs()));
    }
}

void MainWindow::restoreLastProject()
{
    if (globalSettings_.lastProjectRoot.isEmpty()) {
        explorer_->clearRoot();
        return;
    }
    const QFileInfo info(globalSettings_.lastProjectRoot);
    if (!info.exists() || !info.isDir()) {
        explorer_->clearRoot();
        return;
    }
    setProjectRoot(info.absoluteFilePath());
}

void MainWindow::setProjectRoot(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        return;
    }

    projectRoot_ = info.absoluteFilePath();
    orchestrator_.setProjectRoot(projectRoot_);
    explorer_->setRootPath(projectRoot_);
    globalSettings_.lastProjectRoot = projectRoot_;

    VexaraCore::ProjectSettings project;
    project.projectRoot = projectRoot_;
    project.displayName = info.fileName();
    QString projectError;
    if (!project.ensureProjectConfig(&projectError)) {
        statusBar()->showMessage(projectError);
        return;
    }

    terminalDockWidget_->setWorkingDirectory(projectRoot_);
    terminalDockWidget_->panel()->focusCommandLine();
    refreshWindowTitle();
    statusBar()->showMessage(QStringLiteral("Opened folder: %1").arg(projectRoot_));
}

void MainWindow::refreshWindowTitle()
{
    QString title = VexaraCore::AppIdentity::applicationName()
                    + QStringLiteral(" - ")
                    + VexaraCore::AppIdentity::versionLabel();
    if (!projectRoot_.isEmpty()) {
        title += QStringLiteral(" [")
                 + QFileInfo(projectRoot_).fileName()
                 + QStringLiteral("]");
    }
    setWindowTitle(title);
}

} // namespace VexaraEditor
