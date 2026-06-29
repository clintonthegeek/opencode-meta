// ProviderCatalog
// Live provider / model discovery backed by the opencode-managed catalog
// cache (`<Global.Path.cache>/models.json`, see
// docs/OPENCODE-CONFIG-INTROSPECTION.md §8.1 and the live `Provider.Service`
// pipeline at §8.2). Replaces the prior static `models-dev.md` snapshot
// source flagged in report §12.1 item 1.
//
// The catalog is loaded from disk (populated by `opencode models --refresh`).
// Lookups use the exact `Provider.parseModel` first-/-split rule from
// report §8.3 (`[providerID, ...rest] = s.split("/")`, rest joined by `/`),
// so every model id we accept matches what the opencode runtime would
// accept on a `cfg.model`, agent `model`, or `small_model` entry.
//
// Used by:
//   - ContractChecker (opt-in live-validation of emitted model strings)
//   - ModelsBrowserWidget (sole catalog source; the direct
//     `https://models.dev/api.json` HTTP fetch is gone in Phase G1)
//   - Tests + future code paths that need a real lookup

#pragma once

#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

class ProviderCatalog
{
public:
    ProviderCatalog();
    ~ProviderCatalog();

    // Per-model capability flags surfaced by the live catalog. Mirrors
    // the four live flags called out in OpenCode's `ProviderCapabilities`
    // (report §8.5 + PARADIGM §5.6):
    //   - toolcall:    model supports structured tool/function calls
    //                  (read off `tool_call` || `tools` — these are the two
    //                  aliases already used by `ModelsBrowserWidget`)
    //   - reasoning:   model exposes a reasoning trace (`reasoning`)
    //   - attachment:  model accepts file attachments (`attachment`)
    //   - modalities:  flattened union of `modalities.input` and
    //                  `modalities.output`, deduped (e.g. {"text","image"}).
    // A `ModelCapabilities` returned for an unknown / unparseable model id
    // is the default-constructed value (every flag false, modalities empty);
    // callers should treat that case as "capability unknown" rather than
    // "incapable". Used by Phase G4's hot-swap capability matrix to gate
    // model swaps that would otherwise break `edit`/`bash`-enabled
    // Specialists.
    struct ModelCapabilities {
        bool toolcall = false;
        bool reasoning = false;
        bool attachment = false;
        QStringList modalities;
    };

    // Default path for the opencode-managed catalog cache file. Honors
    // XDG_CACHE_HOME on Linux (matches opencode's own `Global.Path.cache` —
    // resolved as `~/.cache/opencode/models.json` in the common case) and
    // falls back to $HOME/.cache on other platforms. Report §8.1.
    static QString defaultCachePath();

    // Process-wide singleton accessor for the live catalog. Lazy-constructs
    // a `ProviderCatalog` on first use and lazy-loads the opencode-managed
    // `<Global.Path.cache>/models.json` (via `defaultCachePath()`) if it
    // hasn't been populated yet. Used by Phase G4's hot-swap warning
    // (TeamEditorWidget::onAddSpecialist) which has no per-instance
    // `m_catalog` member of its own. ModelsBrowserWidget still owns its
    // own instance so its fetches / refreshes stay independent.
    static ProviderCatalog &instance();

    // Run `opencode models --refresh` synchronously via QProcess. On exit,
    // the cache file should be populated. Returns true if the process
    // could be launched (regardless of its exit status — callers should
    // inspect lastRefreshExitCode / lastRefreshError). Empty process path
    // or a failed launch returns false.
    bool refreshFromCli(const QString &opencodeBinaryPath = QString(),
                        int timeoutMs = 30000);

    // Phase C2-1 / D-8: ensure the in-memory catalog is fresh enough for
    // an apply-time validation. If the on-disk cache file at `path`
    // (default = defaultCachePath()) is older than `maxAgeMinutes`, AND
    // an `opencode` binary is available, shells out to
    // `opencode models --refresh` and reloads the resulting file.
    //
    // Returns true if the catalog is now loaded AND its `mtime` is within
    // the freshness window (this is the bar callers should rely on — it
    // means "loadFromCache() succeeded AFTER any refresh we did"). When
    // `maxAgeMinutes` is `<= 0` the freshness check is skipped, i.e. the
    // function behaves the same as `loadFromCache(path)` plus a best-
    // effort refresh on startup.
    //
    // Side-effect free on the in-memory catalog when no refresh happened
    // AND the cache was already loaded: a stale-cache branch that judges
    // the file "fresh enough" leaves the existing `m_root`, `m_providers`,
    // `m_providerModels`, etc. untouched.
    //
    // The helper is the production entry point called by `StorageManager::
    // applyTeamToProject` per D-2 / C2-2; its sole purpose is to push the
    // live catalog closer to "now" before the apply-path ContractChecker
    // gate rejects unseen models.
    bool ensureReadyForApply(int maxAgeMinutes = 60,
                             const QString &path = QString());

    // Read the opencode-managed cache file at `path` (default =
    // defaultCachePath()). Returns true if the JSON document was parsed
    // successfully. An empty catalog is still considered "loaded" for
    // the purposes of subsequent queries (providerIDs() etc. just return
    // an empty list).
    bool loadFromCache(const QString &path = QString());

    // Drop the in-memory snapshot so subsequent queries report empty
    // until the next loadFromCache() or refreshFromCli() roundtrip.
    void clear();

    // Last status of the most recent refresh attempt (-1 if no refresh
    // was attempted, otherwise the QProcess exit code from
    // `opencode models --refresh`).
    int lastRefreshExitCode() const;
    QString lastRefreshError() const;
    QString lastRefreshStandardError() const;

    // True once loadFromCache() has succeeded (even if the catalog
    // document itself is empty).
    bool isLoaded() const;

    // All provider ids at the top level of `<cache>/models.json`
    // (e.g. {"openai","anthropic",…} — 145 providers as of writing).
    QStringList providerIDs() const;

    // Raw provider entry (the JSON object under each provider key). Used
    // by ModelsBrowserWidget to read the display name, env, npm, models
    // map, etc., straight from the parsed snapshot. Returns an empty
    // QJsonObject if the provider is not present.
    QJsonValue providerObject(const QString &providerID) const;

    // Raw provider model entry (the JSON object for a single
    // modelID under a provider's "models" map). Returns an empty
    // QJsonObject if either the provider or the model is not present.
    QJsonValue modelObject(const QString &providerID,
                           const QString &modelID) const;

    // All model ids within a single provider. Returns the keys of the
    // provider's "models" map; for nested provider shapes (e.g.
    // openrouter/anthropic/* or fireworks-ai/accounts/...) the keys
    // already carry the full path so the joined lookup
    // `${providerID}/${modelID}` is well-defined.
    QStringList modelIDs(const QString &providerID) const;

    // True iff `providerModel` matches rule §8.3 (first `/`-split with
    // both halves non-empty) AND the resulting provider is present in
    // the catalog AND the resulting model id exists within that
    // provider. Returns false on an empty input, an unparseable string,
    // an unknown provider, or an unknown model id.
    bool isValidModel(const QString &providerModel) const;

    // Phase G4: extract the four live `ModelCapabilities` flags for a
    // single model. Returns a default-constructed `ModelCapabilities`
    // (every flag false, modalities empty) when `providerModel` fails
    // `parseModel`, the provider is unknown, or the model id is not in
    // the catalog. Callers should treat the default value as "unknown",
    // not as a definitive "incapable" verdict — it's the safe response
    // when the live catalog is missing or stale.
    ModelCapabilities capabilitiesForModel(const QString &providerModel) const;

    // Structurally identical to ContractChecker::parseModel — first `/`
    // splits; provider + model must each be non-empty. Kept here so
    // ProviderCatalog stays self-contained for tests and other callers
    // that don't want to depend on ContractChecker.
    // (Report §8.3 parseModel semantics.)
    static bool parseModel(const QString &modelId,
                           QString *providerId,
                           QString *modelIdOut);

    // Number of providers in the loaded snapshot (0 if not loaded).
    int providerCount() const;
    // Number of models across all providers (0 if not loaded).
    int modelCount() const;

private:
    // Catalog text. The shape mirrors `models.dev/api.json`:
    //   {
    //     "<providerID>": {
    //       "id": "<providerID>",
    //       "name": "...", "env": [...], "npm": "...", "api": "...",
    //       "models": {
    //         "<modelID>": {
    //           "id": "<modelID>", "name": "...", "tool_call": bool,
    //           "cost": {"input":..., "output":...},
    //           "limit": {"context":..., "output":...},
    //           "modalities": {"input":[...], "output":[...]},
    //           ...
    //         }
    //       }
    //     }
    //   }
    QJsonObject m_root;
    QSet<QString> m_providers;
    QMap<QString, QSet<QString>> m_providerModels;
    int m_totalModels = 0;

    int m_lastRefreshExitCode = -1;
    QString m_lastRefreshError;
    QString m_lastRefreshStandardError;
    bool m_loaded = false;

    // Rebuild the fast lookup tables from a freshly-parsed root object.
    // Called from loadFromCache() after QJsonDocument::fromJson succeeds.
    void rebuildIndex(const QJsonObject &root);
};
