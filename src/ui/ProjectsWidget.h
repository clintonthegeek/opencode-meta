 #pragma once

 #include <QWidget>

 #include "models/ProjectRecord.h"

 class QListWidget;
 class QPushButton;
 class StorageManager;

// Projects mode main widget: scan and manage OpenCode projects and active Teams
 class ProjectsWidget : public QWidget
 {
     Q_OBJECT

 public:
     explicit ProjectsWidget(StorageManager &storageManager, QWidget *parent = nullptr);

 private slots:
    void scanForProjects();
    void switchTeamForProject();
    void viewTeamDiffsForProject();
    void toggleWatchForProject();
    void onSelectionChanged();

 private:
     void setupUi();
     void loadProjects();
     void saveProjects() const;
     void refreshList();
     int findProjectIndexByPath(const QString &path) const;
     ProjectRecord *currentProject();
     const ProjectRecord *currentProject() const;

     StorageManager &m_storageManager;
     QList<ProjectRecord> m_projects;

    QListWidget *m_listWidget = nullptr;
    QPushButton *m_scanButton = nullptr;
    QPushButton *m_switchTeamButton = nullptr;
    QPushButton *m_diffButton = nullptr;
    QPushButton *m_watchButton = nullptr;

     QString m_lastScanRoot;
 };
