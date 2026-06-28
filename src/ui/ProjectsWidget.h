 #pragma once

#include <QWidget>

#include "models/ProjectRecord.h"

 class FilterProxyModel;
 class QLineEdit;
 class QListWidget;
 class QPushButton;
 class QSortFilterProxyModel;
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
    // ROADMAP P2-2: case-insensitive dynamic filtering on every column.
    void applyFilter(const QString &text);

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
     QLineEdit *m_filterEdit = nullptr;
     FilterProxyModel *m_filterProxy = nullptr;
     QPushButton *m_scanButton = nullptr;
     QPushButton *m_switchTeamButton = nullptr;
     QPushButton *m_diffButton = nullptr;
     QPushButton *m_watchButton = nullptr;

     QString m_lastScanRoot;
 };
