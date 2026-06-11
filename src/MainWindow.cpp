#include "MainWindow.h"

#include <QLabel>
#include <QTabWidget>

#include "ui/TemplatesWidget.h"
#include "ui/ProfilesWidget.h"
#include "ui/ModelsBrowserWidget.h"
#include "ui/ProjectsWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OpenCode Meta Qt"));
    resize(800, 600);

    m_tabWidget = new QTabWidget(this);

    // Placeholder main tab
    auto *homeLabel = new QLabel(QStringLiteral("OpenCode Meta Qt"), this);
    homeLabel->setAlignment(Qt::AlignCenter);
    m_tabWidget->addTab(homeLabel, tr("Home"));

    // Templates tab
    auto *templatesWidget = new TemplatesWidget(m_storageManager, this);
    m_tabWidget->addTab(templatesWidget, tr("Templates"));

    // Profiles tab — index 2
    auto *profilesWidget = new ProfilesWidget(m_storageManager, this);
    m_tabWidget->addTab(profilesWidget, tr("Profiles"));

    // Projects tab
    auto *projectsWidget = new ProjectsWidget(m_storageManager, this);
    m_tabWidget->addTab(projectsWidget, tr("Projects"));

    // Models tab — index 4
    auto *modelsWidget = new ModelsBrowserWidget(m_storageManager, this);
    m_tabWidget->addTab(modelsWidget, tr("Models"));

    // Cross-mode navigation: Profiles → Models
    connect(profilesWidget, &ProfilesWidget::requestNavigateToModels,
            this, [this]() { m_tabWidget->setCurrentIndex(4); });

    setCentralWidget(m_tabWidget);

    // Ensure storage root directory exists on startup
    m_storageManager.ensureRoot();
}
