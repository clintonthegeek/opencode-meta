#include "ProfilesWidget.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "generation.h"
#include "models/Profile.h"
#include "models/Template.h"
#include "models/ModelInfo.h"
#include "storage/StorageManager.h"
#include "ui/ProfileEditorDialog.h"

namespace {

QString formatProfileSummary(const Profile &p, const QMap<QString, QString> &templateNames)
{
    const QString templateName = templateNames.value(p.templateId, p.templateId);
    const int modelCount = p.modelAssignments.size();
    const QString lastApplied = p.metadata.value(QStringLiteral("last_applied"));

    QString text = p.name;
    if (!templateName.isEmpty()) {
        text += QStringLiteral("  (");
        text += QObject::tr("Template: %1").arg(templateName);
        text += QStringLiteral(", ");
    } else {
        text += QStringLiteral("  (");
    }

    text += QObject::tr("models: %1").arg(modelCount);
    if (!lastApplied.isEmpty()) {
        text += QStringLiteral(", ");
        text += QObject::tr("last applied: %1").arg(lastApplied);
    }
    text += QLatin1Char(')');

    return text;
}

// Copy any prompt files referenced by the template into the global
// opencode config prompts directory (~/.config/opencode/prompts).
void copyTemplatePromptsToGlobal(const Template &t)
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

    const QString configRoot = QDir::homePath() + QStringLiteral("/.config/opencode");
    QDir configDir(configRoot);
    if (!configDir.exists()) {
        configDir.mkpath(QStringLiteral("."));
    }

    const QString promptsRoot = configRoot + QStringLiteral("/prompts");
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

// Write the rendered config to the global opencode.json path.
bool writeGlobalConfig(const QJsonObject &config, const Template &t, QString *errorString = nullptr)
{
    const QString configRoot = QDir::homePath() + QStringLiteral("/.config/opencode");
    QDir configDir(configRoot);
    if (!configDir.exists()) {
        if (!configDir.mkpath(QStringLiteral("."))) {
            if (errorString) {
                *errorString = QObject::tr("Failed to create %1").arg(configRoot);
            }
            return false;
        }
    }

    const QString configFilePath = configRoot + QStringLiteral("/opencode.json");
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
    copyTemplatePromptsToGlobal(t);

    return true;
}

QString defaultModelIdFromCache(const ModelsCache &cache)
{
    if (!cache.models.isEmpty()) {
        auto it = cache.models.constBegin();
        return it.value().id.isEmpty() ? it.key() : it.value().id;
    }

    // Placeholder default until live models integration is wired up.
    return QStringLiteral("xai/grok-1");
}

} // namespace

ProfilesWidget::ProfilesWidget(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    setupUi();
    refreshProfiles();
}

void ProfilesWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listWidget, 1);

    auto *buttonRow = new QHBoxLayout();

    m_createButton = new QPushButton(tr("Create New"), this);
    m_editButton = new QPushButton(tr("Edit"), this);
    m_duplicateButton = new QPushButton(tr("Duplicate"), this);
    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_applyButton = new QPushButton(tr("Apply (Global)"), this);
    m_browseModelsButton = new QPushButton(tr("Browse Models..."), this);
    m_browseModelsButton->setToolTip(tr("Switch to the Models Browser to look up model IDs"));

    buttonRow->addWidget(m_createButton);
    buttonRow->addWidget(m_editButton);
    buttonRow->addWidget(m_duplicateButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addWidget(m_browseModelsButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_applyButton);

    layout->addLayout(buttonRow);

    auto *previewLabel = new QLabel(tr("Preview: rendered opencode.json"), this);
    layout->addWidget(previewLabel);

    m_previewEdit = new QTextEdit(this);
    m_previewEdit->setReadOnly(true);
    layout->addWidget(m_previewEdit, 1);

    connect(m_createButton, &QPushButton::clicked, this, &ProfilesWidget::createProfile);
    connect(m_editButton, &QPushButton::clicked, this, &ProfilesWidget::editSelectedProfile);
    connect(m_duplicateButton, &QPushButton::clicked, this, &ProfilesWidget::duplicateSelectedProfile);
    connect(m_deleteButton, &QPushButton::clicked, this, &ProfilesWidget::deleteSelectedProfile);
    connect(m_applyButton, &QPushButton::clicked, this, &ProfilesWidget::applySelectedProfile);
    connect(m_browseModelsButton, &QPushButton::clicked, this, &ProfilesWidget::requestNavigateToModels);

    connect(m_listWidget, &QListWidget::currentItemChanged, this, &ProfilesWidget::onSelectionChanged);
}

void ProfilesWidget::refreshProfiles()
{
    m_listWidget->clear();

    const QList<Template> templates = m_storageManager.listTemplates();
    QMap<QString, QString> templateNames;
    for (const Template &t : templates) {
        templateNames.insert(t.id, t.name);
    }

    const QList<Profile> profiles = m_storageManager.listProfiles();
    for (const Profile &p : profiles) {
        auto *item = new QListWidgetItem(formatProfileSummary(p, templateNames), m_listWidget);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(p.name);
    }

    updatePreview();
}

QString ProfilesWidget::selectedProfileId() const
{
    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return QString();
    }
    return item->data(Qt::UserRole).toString();
}

void ProfilesWidget::createProfile()
{
    Profile p;

    const QList<Template> templates = m_storageManager.listTemplates();
    if (templates.isEmpty()) {
        QMessageBox::warning(this, tr("Create Profile"), tr("No templates available. Create a template first."));
        return;
    }

    const ModelsCache cache = m_storageManager.loadModelsCache();
    const QString defaultModelId = defaultModelIdFromCache(cache);

    ProfileEditorDialog dlg(templates, p, defaultModelId, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Profile updated = dlg.profileData();
    if (updated.id.isEmpty()) {
        updated.id = updated.name;
    }

    if (updated.id.isEmpty()) {
        QMessageBox::warning(this, tr("Save Profile"), tr("Profile must have a name/id."));
        return;
    }

    if (!m_storageManager.saveProfile(updated)) {
        QMessageBox::warning(this, tr("Save Profile"), tr("Failed to save profile."));
        return;
    }

    refreshProfiles();
}

void ProfilesWidget::editSelectedProfile()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        return;
    }

    Profile p = m_storageManager.loadProfile(id);
    if (p.id.isEmpty()) {
        QMessageBox::warning(this, tr("Edit Profile"), tr("Failed to load profile."));
        return;
    }

    const QList<Template> templates = m_storageManager.listTemplates();
    if (templates.isEmpty()) {
        QMessageBox::warning(this, tr("Edit Profile"), tr("No templates available. Create a template first."));
        return;
    }

    const ModelsCache cache = m_storageManager.loadModelsCache();
    const QString defaultModelId = defaultModelIdFromCache(cache);

    ProfileEditorDialog dlg(templates, p, defaultModelId, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Profile updated = dlg.profileData();
    if (updated.id.isEmpty()) {
        updated.id = p.id;
    }

    if (!m_storageManager.saveProfile(updated)) {
        QMessageBox::warning(this, tr("Save Profile"), tr("Failed to save profile."));
        return;
    }

    refreshProfiles();
}

void ProfilesWidget::duplicateSelectedProfile()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        return;
    }

    Profile p = m_storageManager.loadProfile(id);
    if (p.id.isEmpty()) {
        QMessageBox::warning(this, tr("Duplicate Profile"), tr("Failed to load profile."));
        return;
    }

    p.id.clear();
    p.name += QStringLiteral(" (copy)");

    const QList<Template> templates = m_storageManager.listTemplates();
    if (templates.isEmpty()) {
        QMessageBox::warning(this, tr("Duplicate Profile"), tr("No templates available. Create a template first."));
        return;
    }

    const ModelsCache cache = m_storageManager.loadModelsCache();
    const QString defaultModelId = defaultModelIdFromCache(cache);

    ProfileEditorDialog dlg(templates, p, defaultModelId, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Profile updated = dlg.profileData();
    if (updated.id.isEmpty()) {
        updated.id = updated.name;
    }

    if (updated.id.isEmpty()) {
        QMessageBox::warning(this, tr("Save Profile"), tr("Profile must have a name/id."));
        return;
    }

    if (!m_storageManager.saveProfile(updated)) {
        QMessageBox::warning(this, tr("Save Profile"), tr("Failed to save duplicated profile."));
        return;
    }

    refreshProfiles();
}

void ProfilesWidget::deleteSelectedProfile()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        return;
    }

    const auto reply = QMessageBox::question(this, tr("Delete Profile"), tr("Delete selected profile?"));
    if (reply != QMessageBox::Yes) {
        return;
    }

    const QString path = QDir::homePath() + QStringLiteral("/.opencode-meta/profiles/%1.json").arg(id);
    QFile file(path);
    if (file.exists()) {
        if (!file.remove()) {
            QMessageBox::warning(this, tr("Delete Profile"), tr("Failed to delete profile file."));
        }
    }

    refreshProfiles();
}

void ProfilesWidget::applySelectedProfile()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        return;
    }

    Profile p = m_storageManager.loadProfile(id);
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

    const QString configPath = QDir::homePath() + QStringLiteral("/.config/opencode/opencode.json");
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
    if (!writeGlobalConfig(config, t, &error)) {
        QMessageBox::warning(this, tr("Apply Profile"), error);
        return;
    }

    // Update metadata with last_applied timestamp and persist profile.
    const QString nowIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    p.metadata.insert(QStringLiteral("last_applied"), nowIso);
    m_storageManager.saveProfile(p);

    // Optionally mark as default-profile.json copy.
    m_storageManager.setDefaultProfile(p.id);

    updatePreview();
}

void ProfilesWidget::onSelectionChanged()
{
    updatePreview();
}

void ProfilesWidget::updatePreview()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        m_previewEdit->clear();
        return;
    }

    const Profile p = m_storageManager.loadProfile(id);
    if (p.id.isEmpty() || p.templateId.isEmpty()) {
        m_previewEdit->clear();
        return;
    }

    const Template t = m_storageManager.loadTemplate(p.templateId);
    if (t.id.isEmpty()) {
        m_previewEdit->clear();
        return;
    }

    const QJsonObject config = renderProfileToConfig(t, p);
    const QJsonDocument doc(config);
    m_previewEdit->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}
