#include "storage/StorageManager.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

StorageManager::StorageManager(const QString &rootOverride)
    : m_rootOverride(rootOverride)
{
}

QString StorageManager::rootPath() const
{
    // All data is stored under ~/.opencode-meta/ by default. Tests may inject
    // a different root via m_rootOverride to avoid touching real user data.
    if (!m_rootOverride.isEmpty()) {
        return m_rootOverride;
    }

    return QDir::homePath() + QStringLiteral("/.opencode-meta");
}

void StorageManager::ensureRoot() const
{
    QDir rootDir(rootPath());
    if (rootDir.exists()) {
        return;
    }

    if (!rootDir.mkpath(QStringLiteral("."))) {
        qDebug() << "StorageManager: failed to create root directory" << rootDir.path();
    }
}

bool StorageManager::saveTemplate(const Template &t) const
{
    if (t.id.isEmpty()) {
        qDebug() << "StorageManager: cannot save template without id";
        return false;
    }

    ensureRoot();

    QDir rootDir(rootPath());
    const QString templatesSubdir = QStringLiteral("templates/%1").arg(t.id);
    if (!rootDir.mkpath(templatesSubdir)) {
        qDebug() << "StorageManager: failed to create template directory" << templatesSubdir;
        return false;
    }

    // If any agent prompt is an object containing a {"file": ...} reference,
    // ensure that the prompts/ directory exists for this template.
    bool needsPromptsDir = false;
    for (auto it = t.agents.constBegin(); it != t.agents.constEnd(); ++it) {
        const AgentDef &agent = it.value();
        if (agent.prompt.isObject()) {
            const QJsonObject obj = agent.prompt.toObject();
            if (obj.contains(QStringLiteral("file"))) {
                needsPromptsDir = true;
                break;
            }
        }
    }

    if (needsPromptsDir) {
        const QString promptsSubdir = templatesSubdir + QStringLiteral("/prompts");
        if (!rootDir.mkpath(promptsSubdir)) {
            qDebug() << "StorageManager: failed to create prompts directory" << promptsSubdir;
            // Not a hard failure for saving the template JSON itself, continue.
        }
    }

    const QString templateFilePath = rootPath() + QStringLiteral("/templates/%1/template.json").arg(t.id);
    QFile file(templateFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open template file for writing" << templateFilePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(t.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete template file" << templateFilePath;
        return false;
    }

    return true;
}

Template StorageManager::loadTemplate(const QString &id) const
{
    Template result;

    if (id.isEmpty()) {
        qDebug() << "StorageManager: loadTemplate called with empty id";
        return result;
    }

    const QString templateFilePath = rootPath() + QStringLiteral("/templates/%1/template.json").arg(id);
    QFile file(templateFilePath);
    if (!file.exists()) {
        qDebug() << "StorageManager: template file does not exist" << templateFilePath;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open template file for reading" << templateFilePath << file.errorString();
        return result;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse template JSON" << templateFilePath << parseError.errorString();
        return result;
    }

    result = Template::fromJson(doc.object());
    if (result.id.isEmpty()) {
        result.id = id;
    }

    return result;
}

QList<Template> StorageManager::listTemplates() const
{
    QList<Template> templates;

    const QString templatesPath = rootPath() + QStringLiteral("/templates");
    QDir dir(templatesPath);
    if (!dir.exists()) {
        return templates;
    }

    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        Template t = loadTemplate(entry);
        if (!t.id.isEmpty()) {
            templates.append(t);
        }
    }

    return templates;
}

bool StorageManager::saveProfile(const Profile &p) const
{
    if (p.id.isEmpty()) {
        qDebug() << "StorageManager: cannot save profile without id";
        return false;
    }

    ensureRoot();

    QDir rootDir(rootPath());
    const QString profilesSubdir = QStringLiteral("profiles");
    if (!rootDir.mkpath(profilesSubdir)) {
        qDebug() << "StorageManager: failed to create profiles directory" << profilesSubdir;
        return false;
    }

    const QString profileFilePath = rootPath() + QStringLiteral("/profiles/%1.json").arg(p.id);
    QFile file(profileFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open profile file for writing" << profileFilePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(p.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete profile file" << profileFilePath;
        return false;
    }

    return true;
}

Profile StorageManager::loadProfile(const QString &id) const
{
    Profile result;

    if (id.isEmpty()) {
        qDebug() << "StorageManager: loadProfile called with empty id";
        return result;
    }

    const QString profileFilePath = rootPath() + QStringLiteral("/profiles/%1.json").arg(id);
    QFile file(profileFilePath);
    if (!file.exists()) {
        qDebug() << "StorageManager: profile file does not exist" << profileFilePath;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open profile file for reading" << profileFilePath << file.errorString();
        return result;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse profile JSON" << profileFilePath << parseError.errorString();
        return result;
    }

    result = Profile::fromJson(doc.object());
    if (result.id.isEmpty()) {
        result.id = id;
    }

    return result;
}

QList<Profile> StorageManager::listProfiles() const
{
    QList<Profile> profiles;

    const QString profilesPath = rootPath() + QStringLiteral("/profiles");
    QDir dir(profilesPath);
    if (!dir.exists()) {
        return profiles;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &fileName : files) {
        const QFileInfo info(dir.filePath(fileName));
        const QString id = info.completeBaseName();
        Profile p = loadProfile(id);
        if (!p.id.isEmpty()) {
            profiles.append(p);
        }
    }

    return profiles;
}

bool StorageManager::saveModelsCache(const ModelsCache &cache) const
{
    ensureRoot();

    const QString cacheFilePath = rootPath() + QStringLiteral("/models-cache.json");
    QFile file(cacheFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open models cache file for writing" << cacheFilePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(cache.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete models cache file" << cacheFilePath;
        return false;
    }

    return true;
}

ModelsCache StorageManager::loadModelsCache() const
{
    ModelsCache cache;

    const QString cacheFilePath = rootPath() + QStringLiteral("/models-cache.json");
    QFile file(cacheFilePath);
    if (!file.exists()) {
        // Not an error: cache just has not been created yet.
        return cache;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open models cache file for reading" << cacheFilePath << file.errorString();
        return cache;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse models cache JSON" << cacheFilePath << parseError.errorString();
        return cache;
    }

    cache = ModelsCache::fromJson(doc.object());
    return cache;
}

bool StorageManager::saveProjects(const QList<ProjectRecord> &projects) const
{
    ensureRoot();

    QJsonArray array;
    for (const ProjectRecord &record : projects) {
        array.append(record.toJson());
    }

    const QString projectsFilePath = rootPath() + QStringLiteral("/projects.json");
    QFile file(projectsFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open projects file for writing" << projectsFilePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(array);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete projects file" << projectsFilePath;
        return false;
    }

    return true;
}

QList<ProjectRecord> StorageManager::loadProjects() const
{
    QList<ProjectRecord> projects;

    const QString projectsFilePath = rootPath() + QStringLiteral("/projects.json");
    QFile file(projectsFilePath);
    if (!file.exists()) {
        // Not an error: projects may not have been saved yet.
        return projects;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open projects file for reading" << projectsFilePath << file.errorString();
        return projects;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qDebug() << "StorageManager: failed to parse projects JSON" << projectsFilePath << parseError.errorString();
        return projects;
    }

    if (!doc.isArray()) {
        qDebug() << "StorageManager: projects JSON is not an array" << projectsFilePath;
        return projects;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        projects.append(ProjectRecord::fromJson(value.toObject()));
    }

    return projects;
}

bool StorageManager::setDefaultProfile(const QString &profileId) const
{
    if (profileId.isEmpty()) {
        qDebug() << "StorageManager: setDefaultProfile called with empty profileId";
        return false;
    }

    ensureRoot();

    const QString srcPath = rootPath() + QStringLiteral("/profiles/%1.json").arg(profileId);
    const QString dstPath = rootPath() + QStringLiteral("/default-profile.json");

    if (!QFileInfo::exists(srcPath)) {
        qDebug() << "StorageManager: profile file for default does not exist" << srcPath;
        return false;
    }

    if (QFileInfo::exists(dstPath)) {
        if (!QFile::remove(dstPath)) {
            qDebug() << "StorageManager: failed to remove existing default-profile.json" << dstPath;
            return false;
        }
    }

    if (!QFile::copy(srcPath, dstPath)) {
        qDebug() << "StorageManager: failed to copy default profile" << srcPath << "->" << dstPath;
        return false;
    }

    return true;
}

QString StorageManager::getDefaultProfile() const
{
    const QString dstPath = rootPath() + QStringLiteral("/default-profile.json");
    QFile file(dstPath);
    if (!file.exists()) {
        return QString();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open default-profile.json for reading" << dstPath << file.errorString();
        return QString();
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse default profile JSON" << dstPath << parseError.errorString();
        return QString();
    }

    const QJsonObject obj = doc.object();
    const QString id = obj.value(QStringLiteral("id")).toString();
    return id;
}

// Preferred providers for Models Browser (new)
bool StorageManager::savePreferredProviders(const QSet<QString> &providers) const
{
    ensureRoot();

    QJsonArray array;
    for (const QString &provider : providers) {
        array.append(provider);
    }

    const QString filePath = rootPath() + QStringLiteral("/preferred-providers.json");
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open preferred-providers.json for writing" << filePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(array);
    const QByteArray data = doc.toJson(QJsonDocument::Compact);  // Compact for small set
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write preferred-providers.json" << filePath;
        return false;
    }

    return true;
}

QSet<QString> StorageManager::loadPreferredProviders() const
{
    QSet<QString> providers;

    const QString filePath = rootPath() + QStringLiteral("/preferred-providers.json");
    QFile file(filePath);
    if (!file.exists()) {
        return providers;  // Empty set is default
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open preferred-providers.json for reading" << filePath << file.errorString();
        return providers;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qDebug() << "StorageManager: failed to parse preferred-providers.json" << filePath << parseError.errorString();
        return providers;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (value.isString()) {
            providers.insert(value.toString());
        }
    }

    return providers;
}
