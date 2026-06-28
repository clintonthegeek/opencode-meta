 #pragma once

 #include <QWidget>

 class QListWidget;
 class QPushButton;
 class QLabel;
 class StorageManager;

 // LabOverviewWidget: landing view showing Teams, quick actions, and recent projects.
 //
 // This is intentionally lightweight for the first pass: it reads Teams and
 // Projects via StorageManager and exposes navigation/command signals that
 // MainWindow (or another controller) can connect.
 class LabOverviewWidget : public QWidget
 {
     Q_OBJECT

 public:
     explicit LabOverviewWidget(StorageManager &storageManager,
                                QWidget *parent = nullptr);

 signals:
     void newTeamRequested();
     void browseRolesRequested();
     void openTeamRequested(const QString &teamId);
     void startTrialRequested(const QString &projectPath, const QString &teamId);

 private slots:
     void refreshData();
     void onNewTeamClicked();
     void onBrowseRolesClicked();
     void onTeamActivated();
     void onStartTrialClicked();

 private:
     void setupUi();
     void loadTeams();
     void loadProjects();
     QString currentSelectedTeamId() const;
     QString currentSelectedProjectPath() const;

     StorageManager &m_storageManager;

     QListWidget *m_teamsList = nullptr;
     QListWidget *m_projectsList = nullptr;

     QPushButton *m_newTeamButton = nullptr;
     QPushButton *m_browseRolesButton = nullptr;
     QPushButton *m_startTrialButton = nullptr;

     QLabel *m_teamsHeaderLabel = nullptr;
     QLabel *m_projectsHeaderLabel = nullptr;
 };
