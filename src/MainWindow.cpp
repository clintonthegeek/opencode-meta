#include "MainWindow.h"

#include <QLabel>
#include <QTabWidget>

#include "ui/TemplatesWidget.h"
#include "ui/ProfilesWidget.h"

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

    // Profiles tab
    auto *profilesWidget = new ProfilesWidget(m_storageManager, this);
    m_tabWidget->addTab(profilesWidget, tr("Profiles"));

    setCentralWidget(m_tabWidget);

    // Ensure storage root directory exists on startup
    m_storageManager.ensureRoot();
}
