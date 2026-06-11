 #include "ProjectsWidget.h"

 #include <QDateTime>
 #include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
 #include <QJsonObject>
 #include <QLabel>
 #include <QListWidget>
 #include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QQueue>
#include <QSet>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDialog>

 #include "generation.h"
 #include "models/Profile.h"
 #include "models/Template.h"
 #include "storage/StorageManager.h"

 namespace {

 // Copy any prompt files referenced by the template into the project's
 // local prompts directory: <projectRoot>/prompts.
 void copyTemplatePromptsToProject(const Template &t, const QString &projectRoot)
 {
     if (t.id.isEmpty()) {
         return;
     }

     const QString templatesRoot = QDir::homePath() + QStringLiteral("/.opencode-meta/templates/%1/prompts");
     const QString templatePromptsRoot = templatesRoot.arg(t.id);
     QDir srcDir(templatePromptsRoot);
     if (!srcDir.exists()) {
         // No prompts directory for this template; nothing to copy.
         return;
     }

     QDir projectDir(projectRoot);
     if (!projectDir.exists()) {
         return;
     }

     const QString promptsRoot = projectDir.filePath(QStringLiteral("prompts"));
     QDir dstDir(promptsRoot);
     if (!dstDir.exists()) {
         dstDir.mkpath(QStringLiteral("."));
     }

     // Walk agents and copy only the referenced prompt files.
     for (auto it = t.agents.constBegin(); it != t.agents.constEnd(); ++it) {
         const AgentDef &agent = it.value();
         if (!agent.prompt.isObject()) {
             continue;
         }
         const QJsonObject obj = agent.prompt.toObject();
         const QString fileRel = obj.value(QStringLiteral("file")).toString();
         if (fileRel.isEmpty()) {
             continue;
         }

         const QString srcPath = srcDir.filePath(fileRel);
         QFileInfo srcInfo(srcPath);
         if (!srcInfo.exists() || !srcInfo.isFile()) {
             continue;
         }

         QFileInfo relInfo(fileRel);
         const QString subdir = relInfo.path();
         if (!subdir.isEmpty() && subdir != QLatin1String(".")) {
             dstDir.mkpath(subdir);
         }

         const QString dstPath = dstDir.filePath(fileRel);

         if (QFileInfo::exists(dstPath)) {
             QFile::remove(dstPath);
         }

         QFile::copy(srcPath, dstPath);
     }
 }

 // Write the rendered config to the project's opencode.json path.
 bool writeProjectConfig(const QString &projectRoot, const QJsonObject &config, const Template &t, QString *errorString = nullptr)
 {
     QDir projectDir(projectRoot);
     if (!projectDir.exists()) {
         if (errorString) {
             *errorString = QObject::tr("Project directory does not exist: %1").arg(projectRoot);
         }
         return false;
     }

     const QString configFilePath = projectDir.filePath(QStringLiteral("opencode.json"));
     QFile file(configFilePath);
     if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
         if (errorString) {
             *errorString = QObject::tr("Failed to open %1 for writing: %2").arg(configFilePath, file.errorString());
         }
         return false;
     }

     const QJsonDocument doc(config);
     const QByteArray data = doc.toJson(QJsonDocument::Indented);
     const qint64 written = file.write(data);
     if (written != data.size()) {
         if (errorString) {
             *errorString = QObject::tr("Failed to write complete config to %1").arg(configFilePath);
         }
         return false;
     }

     // Best-effort prompt copy; ignore failures here.
     copyTemplatePromptsToProject(t, projectRoot);

     return true;
 }

 QString formatProjectSummary(const ProjectRecord &record, const StorageManager &storage)
 {
     QString text = record.path;

     QString profileLabel;
     if (!record.profileId.isEmpty()) {
         const Profile p = storage.loadProfile(record.profileId);
         if (!p.id.isEmpty()) {
             profileLabel = p.name;
         } else {
             profileLabel = record.profileId;
         }
     }

     text += QStringLiteral("  (");
     if (!profileLabel.isEmpty()) {
         text += QObject::tr("Profile: %1").arg(profileLabel);
     } else {
         text += QObject::tr("Profile: %1").arg(QObject::tr("none"));
     }

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

     m_listWidget = new QListWidget(this);
     m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
     layout->addWidget(m_listWidget, 1);

     auto *buttonRow = new QHBoxLayout();

     m_scanButton = new QPushButton(tr("Scan"), this);
     m_applyButton = new QPushButton(tr("Apply Profile"), this);
     m_diffButton = new QPushButton(tr("View Diffs"), this);
     m_watchButton = new QPushButton(tr("Set Watch"), this);

     buttonRow->addWidget(m_scanButton);
     buttonRow->addWidget(m_applyButton);
     buttonRow->addWidget(m_diffButton);
     buttonRow->addWidget(m_watchButton);
     buttonRow->addStretch(1);

     layout->addLayout(buttonRow);

     connect(m_scanButton, &QPushButton::clicked, this, &ProjectsWidget::scanForProjects);
     connect(m_applyButton, &QPushButton::clicked, this, &ProjectsWidget::applyProfileToProject);
     connect(m_diffButton, &QPushButton::clicked, this, &ProjectsWidget::viewDiffsForProject);
     connect(m_watchButton, &QPushButton::clicked, this, &ProjectsWidget::toggleWatchForProject);

     connect(m_listWidget, &QListWidget::currentItemChanged, this, &ProjectsWidget::onSelectionChanged);

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
         item->setToolTip(record.path);
     }

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
         // Prefer the workspace root used in this repo.
         defaultRoot = QStringLiteral("/home/clinton/dev");
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

 void ProjectsWidget::applyProfileToProject()
 {
     ProjectRecord *record = currentProject();
     if (!record) {
         return;
     }

     const QList<Profile> profiles = m_storageManager.listProfiles();
     if (profiles.isEmpty()) {
         QMessageBox::warning(this, tr("Apply Profile"), tr("No profiles available. Create a profile first."));
         return;
     }

     QStringList items;
     QStringList ids;
     int currentIndex = 0;
     for (int i = 0; i < profiles.size(); ++i) {
         const Profile &p = profiles.at(i);
         items << QStringLiteral("%1 (%2)").arg(p.name, p.id);
         ids << p.id;
         if (!record->profileId.isEmpty() && p.id == record->profileId) {
             currentIndex = i;
         }
     }

     bool ok = false;
     const QString chosen = QInputDialog::getItem(this,
                                                  tr("Apply Profile"),
                                                  tr("Select profile to apply to project:\n%1").arg(record->path),
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

     const QString profileId = ids.at(idx);
     Profile p = m_storageManager.loadProfile(profileId);
     if (p.id.isEmpty()) {
         QMessageBox::warning(this, tr("Apply Profile"), tr("Failed to load profile."));
         return;
     }

     if (p.templateId.isEmpty()) {
         QMessageBox::warning(this, tr("Apply Profile"), tr("Profile has no base template assigned."));
         return;
     }

     const Template t = m_storageManager.loadTemplate(p.templateId);
     if (t.id.isEmpty()) {
         QMessageBox::warning(this, tr("Apply Profile"), tr("Failed to load base template."));
         return;
     }

     const QJsonObject config = renderProfileToConfig(t, p);

     const QString configPath = QDir(record->path).filePath(QStringLiteral("opencode.json"));
     if (QFileInfo::exists(configPath)) {
         const auto result = QMessageBox::question(this,
                                                   tr("Apply Profile"),
                                                   tr("Overwrite %1?").arg(configPath),
                                                   QMessageBox::Yes | QMessageBox::No,
                                                   QMessageBox::No);
         if (result != QMessageBox::Yes) {
             return;
         }
     }

     QString error;
     if (!writeProjectConfig(record->path, config, t, &error)) {
         QMessageBox::warning(this, tr("Apply Profile"), error);
         return;
     }

     record->profileId = profileId;
     record->lastSync = QDateTime::currentDateTimeUtc();

     saveProjects();
     refreshList();

     QMessageBox::information(this, tr("Apply Profile"), tr("Profile applied to project."));
 }

 void ProjectsWidget::viewDiffsForProject()
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
         QMessageBox::warning(this, tr("View Diffs"), tr("No opencode.json found in project."));
         return;
     }

     QFile file(configFilePath);
     if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
         QMessageBox::warning(this, tr("View Diffs"), tr("Failed to read %1: %2").arg(configFilePath, file.errorString()));
         return;
     }

     const QString currentText = QString::fromUtf8(file.readAll());

     const QList<Profile> profiles = m_storageManager.listProfiles();
     if (profiles.isEmpty()) {
         QMessageBox::warning(this, tr("View Diffs"), tr("No profiles available. Create a profile first."));
         return;
     }

     QStringList items;
     QStringList ids;
     int currentIndex = 0;
     for (int i = 0; i < profiles.size(); ++i) {
         const Profile &p = profiles.at(i);
         items << QStringLiteral("%1 (%2)").arg(p.name, p.id);
         ids << p.id;
         if (!record->profileId.isEmpty() && p.id == record->profileId) {
             currentIndex = i;
         }
     }

     bool ok = false;
     const QString chosen = QInputDialog::getItem(this,
                                                  tr("View Diffs"),
                                                  tr("Select profile to diff against current opencode.json:\n%1").arg(record->path),
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

     const QString profileId = ids.at(idx);
     const Profile p = m_storageManager.loadProfile(profileId);
     if (p.id.isEmpty()) {
         QMessageBox::warning(this, tr("View Diffs"), tr("Failed to load profile."));
         return;
     }

     if (p.templateId.isEmpty()) {
         QMessageBox::warning(this, tr("View Diffs"), tr("Profile has no base template assigned."));
         return;
     }

     const Template t = m_storageManager.loadTemplate(p.templateId);
     if (t.id.isEmpty()) {
         QMessageBox::warning(this, tr("View Diffs"), tr("Failed to load base template."));
         return;
     }

     const QJsonObject config = renderProfileToConfig(t, p);
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
     dlg->setWindowTitle(tr("Profile Diff"));

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
     const bool hasSelection = (m_listWidget && m_listWidget->currentItem());

     if (m_applyButton) {
         m_applyButton->setEnabled(hasSelection);
     }
     if (m_diffButton) {
         m_diffButton->setEnabled(hasSelection);
     }
     if (m_watchButton) {
         m_watchButton->setEnabled(hasSelection);
     }
 }
