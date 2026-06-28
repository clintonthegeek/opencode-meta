 #include "ui/LabOverviewWidget.h"

 #include <QBoxLayout>
 #include <QLabel>
 #include <QListWidget>
 #include <QListWidgetItem>
 #include <QPushButton>

 #include "models/ProjectRecord.h"
 #include "models/Team.h"
 #include "storage/StorageManager.h"

 LabOverviewWidget::LabOverviewWidget(StorageManager &storageManager,
                                      QWidget *parent)
     : QWidget(parent)
     , m_storageManager(storageManager)
 {
     setupUi();
     refreshData();
 }

 void LabOverviewWidget::setupUi()
 {
     auto *rootLayout = new QVBoxLayout(this);
     rootLayout->setContentsMargins(8, 8, 8, 8);
     rootLayout->setSpacing(12);

     // Quick actions row
     auto *actionsLayout = new QHBoxLayout();
     m_newTeamButton = new QPushButton(tr("New Team"), this);
     m_browseRolesButton = new QPushButton(tr("Browse Roles"), this);
     m_startTrialButton = new QPushButton(tr("Start Trial"), this);

     actionsLayout->addWidget(m_newTeamButton);
     actionsLayout->addWidget(m_browseRolesButton);
     actionsLayout->addStretch();
     actionsLayout->addWidget(m_startTrialButton);

     rootLayout->addLayout(actionsLayout);

     // Content: Teams list + Projects list side-by-side
     auto *contentLayout = new QHBoxLayout();

     // Teams column
     auto *teamsLayout = new QVBoxLayout();
     m_teamsHeaderLabel = new QLabel(tr("Teams"), this);
     m_teamsHeaderLabel->setStyleSheet("font-weight: bold");
     m_teamsList = new QListWidget(this);
     m_teamsList->setSelectionMode(QAbstractItemView::SingleSelection);

     teamsLayout->addWidget(m_teamsHeaderLabel);
     teamsLayout->addWidget(m_teamsList);

     // Projects column
     auto *projectsLayout = new QVBoxLayout();
     m_projectsHeaderLabel = new QLabel(tr("Recent Projects"), this);
     m_projectsHeaderLabel->setStyleSheet("font-weight: bold");
     m_projectsList = new QListWidget(this);
     m_projectsList->setSelectionMode(QAbstractItemView::SingleSelection);

     projectsLayout->addWidget(m_projectsHeaderLabel);
     projectsLayout->addWidget(m_projectsList);

     contentLayout->addLayout(teamsLayout, 1);
     contentLayout->addLayout(projectsLayout, 1);

     rootLayout->addLayout(contentLayout, 1);

     connect(m_newTeamButton, &QPushButton::clicked,
             this, &LabOverviewWidget::onNewTeamClicked);
     connect(m_browseRolesButton, &QPushButton::clicked,
             this, &LabOverviewWidget::onBrowseRolesClicked);
     connect(m_startTrialButton, &QPushButton::clicked,
             this, &LabOverviewWidget::onStartTrialClicked);
     connect(m_teamsList, &QListWidget::itemDoubleClicked,
             this, &LabOverviewWidget::onTeamActivated);
 }

 void LabOverviewWidget::refreshData()
 {
     loadTeams();
     loadProjects();
 }

 void LabOverviewWidget::loadTeams()
 {
     m_teamsList->clear();

     const QList<Team> teams = m_storageManager.listTeams();
      for (const Team &team : teams) {
          QString label = team.name.isEmpty() ? team.id : team.name;

          // Lightweight summary of primary models when available.
          if (!team.primarySpecialistIds.isEmpty()) {
              label += QStringLiteral(" — %1 primary specialist(s)").arg(team.primarySpecialistIds.size());
          }

          auto *item = new QListWidgetItem(label, m_teamsList);
          item->setData(Qt::UserRole, team.id);
     }
 }

 void LabOverviewWidget::loadProjects()
 {
     m_projectsList->clear();

     const QList<ProjectRecord> projects = m_storageManager.loadProjects();
      for (const ProjectRecord &project : projects) {
          QString label = project.path;

         // Show currently associated Team id if present.
         if (!project.teamId.isEmpty()) {
             label += QStringLiteral(" — Team: %1").arg(project.teamId);
         }

         auto *item = new QListWidgetItem(label, m_projectsList);
         item->setData(Qt::UserRole, project.path);
         item->setData(Qt::UserRole + 1, project.teamId);
     }
 }

 QString LabOverviewWidget::currentSelectedTeamId() const
 {
     QListWidgetItem *item = m_teamsList->currentItem();
     if (!item) {
         return QString();
     }
     return item->data(Qt::UserRole).toString();
 }

 QString LabOverviewWidget::currentSelectedProjectPath() const
 {
     QListWidgetItem *item = m_projectsList->currentItem();
     if (!item) {
         return QString();
     }
     return item->data(Qt::UserRole).toString();
 }

 void LabOverviewWidget::onNewTeamClicked()
 {
     emit newTeamRequested();
 }

 void LabOverviewWidget::onBrowseRolesClicked()
 {
     emit browseRolesRequested();
 }

 void LabOverviewWidget::onTeamActivated()
 {
     const QString teamId = currentSelectedTeamId();
     if (!teamId.isEmpty()) {
         emit openTeamRequested(teamId);
     }
 }

 void LabOverviewWidget::onStartTrialClicked()
 {
     const QString projectPath = currentSelectedProjectPath();
     if (projectPath.isEmpty()) {
         return;
     }

     // Prefer the Team already associated with the project if present,
     // otherwise fall back to the currently selected Team in the list.
     QString teamId;
     if (QListWidgetItem *item = m_projectsList->currentItem()) {
         teamId = item->data(Qt::UserRole + 1).toString();
     }
     if (teamId.isEmpty()) {
         teamId = currentSelectedTeamId();
     }

     emit startTrialRequested(projectPath, teamId);
 }
