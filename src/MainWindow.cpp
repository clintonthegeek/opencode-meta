#include "MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QTabWidget>

#include "ui/LabOverviewWidget.h"
#include "ui/RolesWidget.h"
#include "ui/TeamsWidget.h"
#include "ui/TrialsWidget.h"
#include "ui/ProjectsWidget.h"
#include "ui/TeamsDialog.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OpenCode Meta Qt"));
    resize(800, 600);

    m_tabWidget = new QTabWidget(this);

    // Ensure storage root and default PARADIGM entities exist before
    // constructing view widgets so initial data (including the Starter
    // Team) is visible as soon as the tabs render.
    m_storageManager.ensureRoot();
    m_storageManager.seedDefaultsIfNeeded();

    // New five-view navigation: Lab Overview, Roles, Teams, Trials, Projects.
    auto *labOverview = new LabOverviewWidget(m_storageManager, this);
    auto *rolesWidget = new RolesWidget(m_storageManager, this);
    auto *teamsWidget = new TeamsWidget(m_storageManager, this);
    auto *trialsWidget = new TrialsWidget(m_storageManager, this);
    auto *projectsWidget = new ProjectsWidget(m_storageManager, this);

    m_tabWidget->addTab(labOverview, tr("Lab Overview"));
    m_tabWidget->addTab(rolesWidget, tr("Roles"));
    m_tabWidget->addTab(teamsWidget, tr("Teams"));
    m_tabWidget->addTab(trialsWidget, tr("Trials"));
    m_tabWidget->addTab(projectsWidget, tr("Projects"));

    // Lab Overview quick actions: route into Roles / Teams views.
    connect(labOverview, &LabOverviewWidget::newTeamRequested,
            this, [this, teamsWidget]() {
                m_tabWidget->setCurrentWidget(teamsWidget);
                teamsWidget->createTeam();
            });

    connect(labOverview, &LabOverviewWidget::browseRolesRequested,
            this, [this, rolesWidget]() {
                m_tabWidget->setCurrentWidget(rolesWidget);
            });

    connect(labOverview, &LabOverviewWidget::openTeamRequested,
            this, [this, teamsWidget](const QString &teamId) {
                m_tabWidget->setCurrentWidget(teamsWidget);
                teamsWidget->selectTeamById(teamId);
            });

    // F2 cross-view wiring: whenever a Team-side flow asks to author a
    // new Role inline (Add Specialist with no Role available, or
    // TeamEditorWidget direct), switch to the Roles tab and seed the
    // editor with the proposed name (empty is acceptable -- the Role
    // editor exposes a name field).
    connect(teamsWidget, &TeamsWidget::createRoleRequested,
            this, [this, rolesWidget](const QString &proposedName) {
                m_tabWidget->setCurrentWidget(rolesWidget);
                rolesWidget->createRole(proposedName);
            });

    // F1 cross-view wiring: when TeamEditorWidget's footer "Apply Team..."
    // button is clicked, open the same TeamsDialog the Teams menu uses,
    // but route the currently loaded Team id through the new
    // setPreselectedTeamId() so the matching row is selected on open.
    // Matches the existing apply path exactly except for the pre-selection.
    connect(teamsWidget, &TeamsWidget::applyTeamRequested,
            this, [this](const QString &teamId) {
                TeamsDialog dlg(m_storageManager, this);
                if (!teamId.isEmpty()) {
                    dlg.setPreselectedTeamId(teamId);
                }
                dlg.exec();
            });

    setCentralWidget(m_tabWidget);

    createMenus();
}

void MainWindow::createMenus()
{
    QMenuBar *bar = menuBar();
    if (!bar) {
        return;
    }

    m_teamsMenu = bar->addMenu(tr("Teams"));

    auto *applyTeamAction = new QAction(tr("Apply Team..."), this);
    m_teamsMenu->addAction(applyTeamAction);

    connect(applyTeamAction, &QAction::triggered, this, [this]() {
        TeamsDialog dlg(m_storageManager, this);
        dlg.exec();
    });
}
