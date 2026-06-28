#include "generation/ProviderCatalog.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QtGlobal>

namespace {

// Locate the opencode binary. Honors $PATH lookup; falls back to the
// common `~/.opencode/bin/opencode` location captured by the installer
// (matches what `opencode debug paths` reports on the agent's host).
QString resolveOpencodeBinary(const QString &override)
{
    if (!override.isEmpty()) {
        return override;
    }
    const QByteArray envPath = qgetenv("OPENCODE_BIN");
    if (!envPath.isEmpty()) {
        return QString::fromLocal8Bit(envPath);
    }
    // Common install locations.
    const QStringList candidates = {
        QDir::homePath() + QStringLiteral("/.opencode/bin/opencode"),
        QStringLiteral("/usr/local/bin/opencode"),
        QStringLiteral("/usr/bin/opencode"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) {
            return c;
        }
    }
    // Fall back to PATH resolution by returning "opencode" and letting
    // QProcess locate via $PATH on Unix.
    return QStringLiteral("opencode");
}

} // namespace

// ============================================================================
// ProviderCatalog — public surface
// ============================================================================

ProviderCatalog::ProviderCatalog() = default;
ProviderCatalog::~ProviderCatalog() = default;

ProviderCatalog &ProviderCatalog::instance()
{
    // Meyers singleton — lazy-construct on first call, safe across DLL
    // boundaries under C++11. The instance lazy-loads the opencode-managed
    // catalog cache on first use so callers don't need to remember to call
    // `loadFromCache()` before asking for capabilities. ModelsBrowserWidget
    // continues to own its own `m_catalog` instance so its refreshes stay
    // independent of the singletons used by hot-swap warning paths.
    static ProviderCatalog catalog;
    if (!catalog.isLoaded()) {
        // Best effort: a missing cache just yields default-constructed
        // capabilities on lookups (declared as "unknown" by callers).
        catalog.loadFromCache(defaultCachePath());
    }
    return catalog;
}

QString ProviderCatalog::defaultCachePath(){
    // OPENCODE_MODELS_PATH overrides everything (report §8.1: "bypass
    // network entirely and read from this path; delete cache if read
    // fails"). We treat it as a candidate for the cache file too if it
    // exists, otherwise we fall through to the default cache location.
    const QByteArray modelsPath = qgetenv("OPENCODE_MODELS_PATH");
    if (!modelsPath.isEmpty()) {
        const QString candidate = QString::fromLocal8Bit(modelsPath);
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    // Build <Global.Path.cache>/models.json. Report §8.1 says the cache
    // lives at <Global.Path.cache>/models.json on Linux/macOS. The same
    // convention leaks into Tier-1 SDK globals; we reproduce it as
    // `$XDG_CACHE_HOME/opencode/models.json` with `~/.cache/opencode/models.json`
    // as the default (per `opencode debug paths` on the dev box).
    QString base;
    const QByteArray xdg = qgetenv("XDG_CACHE_HOME");
    if (!xdg.isEmpty()) {
        base = QString::fromLocal8Bit(xdg);
    } else {
        base = QDir::homePath() + QStringLiteral("/.cache");
    }
    return base + QStringLiteral("/opencode/models.json");
}

bool ProviderCatalog::refreshFromCli(const QString &opencodeBinaryPath,
                                     int timeoutMs)
{
    m_lastRefreshExitCode = -1;
    m_lastRefreshError.clear();
    m_lastRefreshStandardError.clear();

    const QString bin = resolveOpencodeBinary(opencodeBinaryPath);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    QStringList args;
    args << QStringLiteral("models") << QStringLiteral("--refresh");
    proc.start(bin, args);
    if (!proc.waitForStarted(5000)) {
        m_lastRefreshError = QStringLiteral("could not start %1: %2")
                                 .arg(bin, proc.errorString());
        return false;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        m_lastRefreshError = QStringLiteral(
            "opencode models --refresh timed out after %1 ms").arg(timeoutMs);
        return false;
    }
    m_lastRefreshExitCode = proc.exitCode();
    m_lastRefreshStandardError = QString::fromLocal8Bit(proc.readAllStandardError());
    if (m_lastRefreshExitCode != 0) {
        m_lastRefreshError = QStringLiteral(
            "opencode models --refresh exited %1: %2")
            .arg(m_lastRefreshExitCode)
            .arg(m_lastRefreshStandardError.isEmpty()
                     ? proc.readAllStandardOutput()
                     : m_lastRefreshStandardError);
    }
    return true; // launch succeeded; caller should still inspect exit code
}

bool ProviderCatalog::loadFromCache(const QString &path)
{
    const QString resolvedPath = path.isEmpty() ? defaultCachePath() : path;
    QFile f(resolvedPath);
    if (!f.open(QIODevice::ReadOnly)) {
        // Missing cache is fine — UI just shows empty until refresh runs.
        // We do NOT mark m_loaded = true because there is no catalog.
        m_loaded = false;
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        m_loaded = false;
        return false;
    }
    rebuildIndex(doc.object());
    m_loaded = true;
    return true;
}

void ProviderCatalog::clear()
{
    m_root = QJsonObject();
    m_providers.clear();
    m_providerModels.clear();
    m_totalModels = 0;
    m_loaded = false;
}

int ProviderCatalog::lastRefreshExitCode() const { return m_lastRefreshExitCode; }
QString ProviderCatalog::lastRefreshError() const { return m_lastRefreshError; }
QString ProviderCatalog::lastRefreshStandardError() const
{
    return m_lastRefreshStandardError;
}

bool ProviderCatalog::isLoaded() const { return m_loaded; }

QStringList ProviderCatalog::providerIDs() const
{
    QStringList ids(m_providers.begin(), m_providers.end());
    ids.sort();
    return ids;
}

QJsonValue ProviderCatalog::providerObject(const QString &providerID) const
{
    if (!m_providers.contains(providerID)) {
        return QJsonValue();
    }
    return m_root.value(providerID);
}

QJsonValue ProviderCatalog::modelObject(const QString &providerID,
                                       const QString &modelID) const
{
    const QJsonObject pobj = m_root.value(providerID).toObject();
    if (pobj.isEmpty()) {
        return QJsonValue();
    }
    const QJsonObject m = pobj.value(QStringLiteral("models")).toObject();
    if (!m.contains(modelID)) {
        return QJsonValue();
    }
    return m.value(modelID);
}

QStringList ProviderCatalog::modelIDs(const QString &providerID) const
{
    QStringList ids;
    const auto it = m_providerModels.constFind(providerID);
    if (it == m_providerModels.constEnd()) {
        return ids;
    }
    ids = QStringList(it.value().begin(), it.value().end());
    ids.sort();
    return ids;
}

bool ProviderCatalog::isValidModel(const QString &providerModel) const
{
    QString provider;
    QString model;
    if (!parseModel(providerModel, &provider, &model)) {
        return false;
    }
    if (!m_providers.contains(provider)) {
        return false;
    }
    const auto it = m_providerModels.constFind(provider);
    if (it == m_providerModels.constEnd()) {
        return false;
    }
    return it.value().contains(model);
}

bool ProviderCatalog::parseModel(const QString &modelId,
                                 QString *providerId,
                                 QString *modelIdOut)
{
    // Mirrors report §8.3 + ContractChecker::parseModel. First '/' wins.
    if (modelId.isEmpty()) {
        return false;
    }
    const int slashIndex = modelId.indexOf(QLatin1Char('/'));
    if (slashIndex <= 0 || slashIndex == modelId.size() - 1) {
        return false;
    }
    if (providerId) {
        *providerId = modelId.left(slashIndex);
    }
    if (modelIdOut) {
        *modelIdOut = modelId.mid(slashIndex + 1);
    }
    return true;
}

ProviderCatalog::ModelCapabilities
ProviderCatalog::capabilitiesForModel(const QString &providerModel) const
{
    // Default value for the "unknown" cases (unparseable id, missing
    // provider, missing model under that provider, or the catalog was
    // never loaded in the first place). Callers should treat this as
    // "capability unknown" — not as an authoritative "false" — so a
    // missing cache never silently downgrades a Specialist.
    ModelCapabilities caps;

    QString provider;
    QString modelID;
    if (!parseModel(providerModel, &provider, &modelID)) {
        return caps;
    }

    const QJsonValue v = modelObject(provider, modelID);
    if (!v.isObject()) {
        return caps;
    }
    const QJsonObject m = v.toObject();

    // `tool_call` is the canonical models.dev field name; `tools` is the
    // mirror used by ModelsBrowserWidget when `tool_call` is missing.
    // ModelsBrowserWidget.cpp:311 || 384 treats either as "tool-use".
    // We follow the same convention so the warning dialog stays consistent
    // with the picker UI's Capabilities column.
    caps.toolcall = m.value(QStringLiteral("tool_call")).toBool()
                 || m.value(QStringLiteral("tools")).toBool();
    caps.reasoning = m.value(QStringLiteral("reasoning")).toBool();
    caps.attachment = m.value(QStringLiteral("attachment")).toBool();

    // Flatten modalities.input ∪ modalities.output (`text`/`image`/
    // `audio`/`video`/`pdf` per report §8.5). Dedup so a model that lists
    // `text` in both input and output doesn't double-count.
    const QJsonObject mod = m.value(QStringLiteral("modalities")).toObject();
    QSet<QString> seen;
    const auto appendAll = [&](const QString &key) {
        const QJsonArray arr = mod.value(key).toArray();
        for (const QJsonValue &val : arr) {
            if (!val.isString()) {
                continue;
            }
            const QString s = val.toString();
            if (s.isEmpty() || seen.contains(s)) {
                continue;
            }
            seen.insert(s);
            caps.modalities.append(s);
        }
    };
    appendAll(QStringLiteral("input"));
    appendAll(QStringLiteral("output"));

    return caps;
}

int ProviderCatalog::providerCount() const { return int(m_providers.size()); }
int ProviderCatalog::modelCount() const { return m_totalModels; }

// ============================================================================
// ProviderCatalog — index rebuild
// ============================================================================

void ProviderCatalog::rebuildIndex(const QJsonObject &root)
{
    // Not exposed in the header — implemented as a private helper that
    // populates the fast lookup tables after a successful JSON parse.
    // Kept here in the .cpp for unit-test reachability (PIMPL-free).
    m_root = root;
    m_providers.clear();
    m_providerModels.clear();
    m_totalModels = 0;
    for (auto pit = root.constBegin(); pit != root.constEnd(); ++pit) {
        const QString providerID = pit.key();
        const QJsonValue pval = pit.value();
        if (!pval.isObject()) {
            continue;
        }
        const QJsonObject pobj = pval.toObject();
        const QJsonValue mval = pobj.value(QStringLiteral("models"));
        QSet<QString> modelSet;
        if (mval.isObject()) {
            const QJsonObject mobj = mval.toObject();
            for (auto mit = mobj.constBegin(); mit != mobj.constEnd(); ++mit) {
                modelSet.insert(mit.key());
                ++m_totalModels;
            }
        }
        if (!modelSet.isEmpty()) {
            m_providers.insert(providerID);
            m_providerModels.insert(providerID, modelSet);
        } else if (pobj.contains(QStringLiteral("id"))) {
            // Provider with no models (rb empty catalogs) still counts.
            m_providers.insert(providerID);
        }
    }
}
