#include "ProjectsWidget.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
 #include <QJsonObject>
 #include <QLineEdit>
 #include <QListWidget>
 #include <QListWidgetItem>
 #include <QMessageBox>
#include <QPushButton>
#include <QQueue>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDialog>

#include "apply_helpers.h"
#include "generation/TeamRenderer.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/ConfirmApplyDialog.h"
#include "ui/FilterBar.h"
// ApplyProfileDialog has been moved to legacy/; Team-based switching is
// gated through ConfirmApplyDialog (ROADMAP P1-5).

 namespace {

 QString activeTeamLabel(const ProjectRecord &record, const StorageManager &storage)
 {
     const QString teamId = !record.teamId.isEmpty() ? record.teamId : record.profileId;
     if (teamId.isEmpty()) {
         return QObject::tr("none");
     }

     const Team team = storage.loadTeam(teamId);
     if (team.id.isEmpty()) {
         return teamId;
     }

     return team.name.isEmpty() ? team.id : team.name;
 }

 QString formatProjectSummary(const ProjectRecord &record, const StorageManager &storage)
 {
     QString text = record.path;

     text += QStringLiteral("  (");
     text += QObject::tr("Active Team: %1").arg(activeTeamLabel(record, storage));

     text += QStringLiteral(", ");

     if (record.lastSync.isValid()) {
         text += QObject::tr("last sync: %1").arg(record.lastSync.toString(Qt::ISODate));
     } else {
         text += QObject::tr("last sync: %1").arg(QObject::tr("never"));
     }

     text += QStringLiteral(")");

     if (record.watchEnabled) {
         text += QStringLiteral("  [watch]");
     }

     return text;
 }

 QString buildProjectTooltip(const ProjectRecord &record, const StorageManager &storage)
 {
     QStringList lines;
     lines << record.path;
     lines << QObject::tr("Active Team: %1").arg(activeTeamLabel(record, storage));
     lines << QObject::tr("Last sync: %1").arg(record.lastSync.isValid() ? record.lastSync.toString(Qt::ISODate)
                                                                           : QObject::tr("never"));
     lines << QObject::tr("Last Trial: %1").arg(record.lastTrialId.isEmpty() ? QObject::tr("never") : record.lastTrialId);
     lines << QObject::tr("Watch: %1").arg(record.watchEnabled ? QObject::tr("enabled") : QObject::tr("disabled"));
     return lines.join(QLatin1Char('\n'));
 }

 QJsonObject renderTeamConfig(const Team &team, StorageManager &storage)
 {
     QSet<QString> specialistIds;
     QSet<QString> roleIds;
      for (const auto &binding : team.specialists) {
          const QString roleId = binding.roleId;
          const QString specialistId = binding.specialistId;
          if (!roleId.isEmpty()) {
              roleIds.insert(roleId);
          }
          if (!specialistId.isEmpty()) {
              specialistIds.insert(specialistId);
          }
      }

     QMap<QString, Specialist> specialists;
     for (const QString &specId : std::as_const(specialistIds)) {
         const Specialist s = storage.loadSpecialist(specId);
         if (!s.id.isEmpty()) {
             specialists.insert(s.id, s);
         }
     }

     QMap<QString, Role> roles;
     for (const QString &roleId : std::as_const(roleIds)) {
         const Role r = storage.loadRole(roleId);
         if (!r.id.isEmpty()) {
             roles.insert(r.id, r);
         }
     }

     return TeamRenderer::render(team, specialists, roles);
 }

 void populateDiffEditor(QTextEdit *edit,
                         const QStringList &lines,
                         const QVector<bool> &isDifferent,
                         const QColor &diffColor)
 {
     edit->clear();
     edit->setReadOnly(true);

     QTextCharFormat normalFormat;
     QTextCharFormat diffFormat;
     diffFormat.setBackground(diffColor);

     QTextDocument *doc = edit->document();
     QTextCursor cursor(doc);

     for (int i = 0; i < lines.size(); ++i) {
         const bool different = (i < isDifferent.size()) ? isDifferent[i] : false;
         cursor.setCharFormat(different ? diffFormat : normalFormat);
         cursor.insertText(lines.at(i));
         if (i + 1 < lines.size()) {
             cursor.insertBlock();
         }
     }
 }

 } // namespace

 ProjectsWidget::ProjectsWidget(StorageManager &storageManager, QWidget *parent)
     : QWidget(parent)
     , m_storageManager(storageManager)
 {
     setupUi();
     loadProjects();
     refreshList();
 }

void ProjectsWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    // ROADMAP P2-2: filter bar sits above the project list and drives
    // a QSortFilterProxyModel whose accept/reject decisions map onto
    // QListWidgetItem::setHidden() so hidden rows disappear from
    // navigation and selection without losing their stored data.
    auto *filterBar = new FilterBar(tr("Filter projects..."), this);
    m_filterEdit = filterBar->findChild<QLineEdit *>();
    layout->addWidget(filterBar);

      m_listWidget = new QListWidget(this);
      m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
      layout->addWidget(m_listWidget, 1);

      auto *buttonRow = new QHBoxLayout();

      m_scanButton = new QPushButton(tr("Scan"), this);
      m_switchTeamButton = new QPushButton(tr("Switch Team"), this);
      m_switchTeamButton->setToolTip(tr("Switch the active Team for the selected project. This creates a Trial record."));
      m_diffButton = new QPushButton(tr("View Team Diffs"), this);
      m_diffButton->setToolTip(tr("Preview the selected Team against the project's current opencode.json."));
      m_watchButton = new QPushButton(tr("Set Watch"), this);

      buttonRow->addWidget(m_scanButton);
      buttonRow->addWidget(m_switchTeamButton);
      buttonRow->addWidget(m_diffButton);
      buttonRow->addWidget(m_watchButton);
      buttonRow->addStretch(1);

      layout->addLayout(buttonRow);

      connect(m_scanButton, &QPushButton::clicked, this, &ProjectsWidget::scanForProjects);
      connect(m_switchTeamButton, &QPushButton::clicked, this, &ProjectsWidget::switchTeamForProject);
      connect(m_diffButton, &QPushButton::clicked, this, &ProjectsWidget::viewTeamDiffsForProject);
      connect(m_watchButton, &QPushButton::clicked, this, &ProjectsWidget::toggleWatchForProject);

     connect(m_listWidget, &QListWidget::currentItemChanged, this, &ProjectsWidget::onSelectionChanged);

     // Filter engine. QListWidget's underlying model is wrapped but the
     // widget itself remains the view, so currentItemChanged() and
     // currentItem() keep working unchanged.
     m_filterProxy = new FilterProxyModel(this);
     m_filterProxy->setSourceModel(m_listWidget->model());
     m_filterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
     m_filterProxy->setFilterKeyColumn(-1); // every column

     connect(filterBar, &FilterBar::filterChanged,
             this, &ProjectsWidget::applyFilter);

     onSelectionChanged();
}

 void ProjectsWidget::loadProjects()
 {
     m_projects = m_storageManager.loadProjects();
 }

 void ProjectsWidget::saveProjects() const
 {
     m_storageManager.saveProjects(m_projects);
 }

void ProjectsWidget::refreshList()
{
    m_listWidget->clear();

      for (const ProjectRecord &record : m_projects) {
          auto *item = new QListWidgetItem(formatProjectSummary(record, m_storageManager), m_listWidget);
          item->setData(Qt::UserRole, record.path);
          item->setToolTip(buildProjectTooltip(record, m_storageManager));
      }

     // Re-apply the active filter so freshly scanned and freshly toggled
     // projects honor the current search before the user types again.
     applyFilter(m_filterEdit ? m_filterEdit->text() : QString());

     onSelectionChanged();
}

 int ProjectsWidget::findProjectIndexByPath(const QString &path) const
 {
     for (int i = 0; i < m_projects.size(); ++i) {
         if (QDir::cleanPath(m_projects.at(i).path) == QDir::cleanPath(path)) {
             return i;
         }
     }
     return -1;
 }

 ProjectRecord *ProjectsWidget::currentProject()
 {
     const ProjectRecord *cp = const_cast<const ProjectsWidget *>(this)->currentProject();
     return const_cast<ProjectRecord *>(cp);
 }

const ProjectRecord *ProjectsWidget::currentProject() const
{
    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return nullptr;
    }

    // Skip rows the active filter has hidden so callers cannot operate
    // on a project the user is not currently viewing.
    if (item->isHidden()) {
        return nullptr;
    }

    const QString path = item->data(Qt::UserRole).toString();
    const int index = findProjectIndexByPath(path);
    if (index < 0 || index >= m_projects.size()) {
        return nullptr;
    }

    return &m_projects.at(index);
}

 void ProjectsWidget::scanForProjects()
 {
    QString defaultRoot;
    if (!m_lastScanRoot.isEmpty()) {
        defaultRoot = m_lastScanRoot;
    } else {
        // Fall back to the user's home directory when no previous scan root exists.
        defaultRoot = QDir::homePath();
    }

     bool ok = false;
     const QString input = QInputDialog::getText(this,
                                                 tr("Scan for Projects"),
                                                 tr("Root directory to scan:"),
                                                 QLineEdit::Normal,
                                                 defaultRoot,
                                                 &ok);
     const QString rootPath = input.trimmed();
     if (!ok || rootPath.isEmpty()) {
         return;
     }

     QDir rootDir(rootPath);
     if (!rootDir.exists()) {
         QMessageBox::warning(this, tr("Scan for Projects"), tr("Directory does not exist: %1").arg(rootPath));
         return;
     }

     m_lastScanRoot = rootDir.absolutePath();

    QSet<QString> foundPaths;

    const QString rootAbs = rootDir.absolutePath();

    // Breadth-first traversal up to depth 3 from root to avoid
    // walking the entire filesystem tree.
    struct PendingDir {
        QString path;
        int depth = 0;
    };

    QQueue<PendingDir> queue;
    queue.enqueue({rootAbs, 0});

    while (!queue.isEmpty()) {
        const PendingDir current = queue.dequeue();

        QDir dir(current.path);
        if (!dir.exists()) {
            continue;
        }

        const bool hasConfigFile = dir.exists(QStringLiteral("opencode.json"));
        const bool hasConfigDir = dir.exists(QStringLiteral(".opencode"));
        if (hasConfigFile || hasConfigDir) {
            foundPaths.insert(QDir::cleanPath(current.path));
        }

        if (current.depth >= 3) {
            continue;
        }

        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &subdirInfo : subdirs) {
            queue.enqueue({subdirInfo.absoluteFilePath(), current.depth + 1});
        }
    }

     if (foundPaths.isEmpty()) {
         QMessageBox::information(this, tr("Scan for Projects"), tr("No OpenCode projects found under %1").arg(rootAbs));
         return;
     }

     bool changed = false;
     for (const QString &path : std::as_const(foundPaths)) {
         if (findProjectIndexByPath(path) >= 0) {
             continue;
         }

         ProjectRecord record;
         record.path = path;
         record.watchEnabled = false;

         m_projects.append(record);
         changed = true;
     }

     if (changed) {
         saveProjects();
         refreshList();
     }
 }

void ProjectsWidget::switchTeamForProject()
{
      ProjectRecord *record = currentProject();
      if (!record) {
          return;
      }

      const QList<Team> teams = m_storageManager.listTeams();
      if (teams.isEmpty()) {
          QMessageBox::warning(this, tr("Switch Team"), tr("No Teams available. Create a Team first."));
          return;
      }

      QStringList items;
      QStringList ids;
      int currentIndex = 0;
      for (int i = 0; i < teams.size(); ++i) {
          const Team &team = teams.at(i);
          items << QStringLiteral("%1 (%2)").arg(team.name.isEmpty() ? team.id : team.name, team.id);
          ids << team.id;
          if (!record->teamId.isEmpty() && team.id == record->teamId) {
              currentIndex = i;
          }
      }

      bool ok = false;
      const QString chosen = QInputDialog::getItem(this,
                                                   tr("Switch Team"),
                                                   tr("Select Team to switch to for project:\n%1").arg(record->path),
                                                   items,
                                                   currentIndex,
                                                   false,
                                                   &ok);
     if (!ok || chosen.isEmpty()) {
         return;
     }

      const int idx = items.indexOf(chosen);
      if (idx < 0 || idx >= ids.size()) {
          return;
      }

      const QString teamId = ids.at(idx);
      const Team team = m_storageManager.loadTeam(teamId);
      if (team.id.isEmpty()) {
          QMessageBox::warning(this, tr("Switch Team"), tr("Failed to load Team."));
          return;
      }

     const QDir projectDir(record->path);
     const QString configPath = projectDir.filePath(QStringLiteral("opencode.json"));

     // Capture the existing on-disk contents (if any) so the
     // ConfirmApplyDialog can show the diff before the apply writes
     // anything. ROADMAP P1-5 makes this the gate: nothing is written
     // until the user accepts the dialog.
     QString existingText;
     bool existingIsJson = false;

     if (QFileInfo::exists(configPath)) {
         QFile file(configPath);
         if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
             existingText = QString::fromUtf8(file.readAll());

             QJsonParseError parseError{};
             const QJsonDocument doc = QJsonDocument::fromJson(existingText.toUtf8(), &parseError);
             if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                 existingIsJson = true;
             }
         }
     }

     ConfirmApplyDialog dlg(record->path,
                            team,
                            m_storageManager,
                            existingText,
                            existingIsJson,
                            this);
     if (dlg.exec() != QDialog::Accepted) {
         // User cancelled — no file IO, no project-state change, no Trial.
         return;
     }

     if (!m_storageManager.applyTeamToProject(record->path, team.id)) {
         QMessageBox::warning(this,
                              tr("Switch Team"),
                              tr("Failed to switch Team for project."));
         return;
     }

     loadProjects();
     refreshList();

     QMessageBox::information(this, tr("Switch Team"), tr("Team switched for project. Trial recorded."));
   }

  void ProjectsWidget::viewTeamDiffsForProject()
  {
      const ProjectRecord *record = currentProject();
     if (!record) {
         return;
     }

      const QString projectRoot = record->path;
      QDir projectDir(projectRoot);
      const QString configFilePath = projectDir.filePath(QStringLiteral("opencode.json"));

     QFileInfo info(configFilePath);
      if (!info.exists() || !info.isFile()) {
          QMessageBox::warning(this, tr("View Team Diffs"), tr("No opencode.json found in project."));
          return;
      }

     QFile file(configFilePath);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
          QMessageBox::warning(this, tr("View Team Diffs"), tr("Failed to read %1: %2").arg(configFilePath, file.errorString()));
          return;
      }

     const QString currentText = QString::fromUtf8(file.readAll());

      const QList<Team> teams = m_storageManager.listTeams();
      if (teams.isEmpty()) {
          QMessageBox::warning(this, tr("View Team Diffs"), tr("No Teams available. Create a Team first."));
          return;
      }

      QStringList items;
      QStringList ids;
      int currentIndex = 0;
      for (int i = 0; i < teams.size(); ++i) {
          const Team &team = teams.at(i);
          items << QStringLiteral("%1 (%2)").arg(team.name.isEmpty() ? team.id : team.name, team.id);
          ids << team.id;
          if (!record->teamId.isEmpty() && team.id == record->teamId) {
              currentIndex = i;
          }
      }

      bool ok = false;
      const QString chosen = QInputDialog::getItem(this,
                                                   tr("View Team Diffs"),
                                                   tr("Select Team to diff against current opencode.json:\n%1").arg(record->path),
                                                   items,
                                                   currentIndex,
                                                   false,
                                                   &ok);
     if (!ok || chosen.isEmpty()) {
         return;
     }

      const int idx = items.indexOf(chosen);
      if (idx < 0 || idx >= ids.size()) {
          return;
      }

      const QString teamId = ids.at(idx);
      const Team team = m_storageManager.loadTeam(teamId);
      if (team.id.isEmpty()) {
          QMessageBox::warning(this, tr("View Team Diffs"), tr("Failed to load Team."));
          return;
      }

      const QJsonObject config = renderTeamConfig(team, m_storageManager);
      const QJsonDocument doc(config);
      const QString renderedText = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

     const QStringList currentLines = currentText.split(QLatin1Char('\n'));
     const QStringList renderedLines = renderedText.split(QLatin1Char('\n'));

     const int maxLines = qMax(currentLines.size(), renderedLines.size());
     QVector<bool> differentFlags(maxLines, false);
     for (int i = 0; i < maxLines; ++i) {
         const QString left = (i < currentLines.size()) ? currentLines.at(i) : QString();
         const QString right = (i < renderedLines.size()) ? renderedLines.at(i) : QString();
         if (left != right) {
             differentFlags[i] = true;
         }
     }

      auto *dlg = new QDialog(this);
      dlg->setWindowTitle(tr("Team Diff"));

     auto *layout = new QHBoxLayout(dlg);
     auto *leftEdit = new QTextEdit(dlg);
     auto *rightEdit = new QTextEdit(dlg);

     populateDiffEditor(leftEdit, currentLines, differentFlags, QColor(255, 200, 200));
     populateDiffEditor(rightEdit, renderedLines, differentFlags, QColor(200, 255, 200));

     layout->addWidget(leftEdit);
     layout->addWidget(rightEdit);

      dlg->resize(1000, 600);
      dlg->exec();
  }

 void ProjectsWidget::toggleWatchForProject()
 {
     ProjectRecord *record = currentProject();
     if (!record) {
         return;
     }

     record->watchEnabled = !record->watchEnabled;
     saveProjects();
     refreshList();
 }

void ProjectsWidget::onSelectionChanged()
{
     // Treat "something is selected AND it is visible" as the
     // selection state so the action buttons cannot target a project
     // that the current filter has hidden.
     QListWidgetItem *current = m_listWidget ? m_listWidget->currentItem() : nullptr;
     const bool hasSelection = (current != nullptr) && !current->isHidden();

      if (m_switchTeamButton) {
          m_switchTeamButton->setEnabled(hasSelection);
      }
     if (m_diffButton) {
         m_diffButton->setEnabled(hasSelection);
     }
     if (m_watchButton) {
         m_watchButton->setEnabled(hasSelection);
     }
}

void ProjectsWidget::applyFilter(const QString &text)
{
    if (!m_listWidget || !m_filterProxy) {
        return;
    }

    const QString needle = text.trimmed();
    m_filterProxy->setFilterFixedString(needle);

    // Drive item visibility from the proxy's accept/reject decisions.
    // QListWidget only has a single logical column, so this works
    // whether filterKeyColumn is 0 or -1.
    const QModelIndex parent;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        const bool match = needle.isEmpty() || m_filterProxy->acceptsRow(i, parent);
        item->setHidden(!match);
    }

    // Visibility just changed; make sure the action buttons reflect
    // the new visible selection.
    onSelectionChanged();
}
