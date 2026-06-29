// ContractChecker
// Validates a candidate opencode.json object against the binding contract
// pinned by /home/clinton/dev/opencode-meta/docs/OPENCODE-CONFIG-INTROSPECTION.md
// (the "introspection report"). This checker enforces the current §12.3
// one-line rule and refuses to write a file that would trip
// parseConfig(Schema). REJECT unknown keys.
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <vector>

class ProviderCatalog;

struct ContractCheckResult
{
    bool ok = true;
    QStringList errors;
    QStringList warnings;

    static ContractCheckResult pass();
    static ContractCheckResult fail(QStringList errs);
    static ContractCheckResult failOne(const QString &err);

    // Combine sub-results: any failure fails the whole.
    void merge(const ContractCheckResult &other);
};

// ContractChecker — pure-JSON validator modeled on opencode's
// ConfigParse.schema. Walks:
//   * Top-level keys vs. report §4 (ConfigV1.Info allowed field set).
//   * Per-agent entry fields vs. report §7.1 (ConfigAgentV1.Info KNOWN_KEYS).
//   * Per-agent permission object vs. report §6.1 (15 legal keys + action
//     vocabulary "ask"|"allow"|"deny" + per-key pattern form for the 10
//     rule-shaped keys).
//   * Every "model" string vs. report §8.3 (Provider.parseModel semantics:
//     first "/" splits; provider + model segments must each be non-empty).
//
// The check is intentionally strict: unknown top-level keys, unknown agent
// fields, unknown permission keys, illegal mode values, illegal action
// vocabulary, and malformed model strings all produce errors. Renderer bugs
// surface immediately instead of being silently routed into the runtime's
// `options` rest bag.
class ContractChecker
{
public:
    ContractChecker();
    ~ContractChecker();

    // Primary entry point (per G3 spec). Returns true when `config`
    // satisfies every rule from docs/OPENCODE-CONFIG-INTROSPECTION.md
    // §3, §4, §6.1, §7.1, §8.3 / §12.3; returns false on the first
    // violation and populates *errorMessage (when non-null) with a
    // human-readable reason. Side-effect free.
    static bool validate(const QJsonObject &config, QString *errorMessage = nullptr);

    // G1-strict variant: when `catalog` is non-null and loaded, every
    // model string is also checked against the live `<Global.Path.cache>
    // /models.json` catalog. When catalog is null OR it failed to load,
    // the existing structural §8.3 check is used (so callers without a
    // live catalog continue to get the structural gate the G3 spec
    // locked down). Returns the per-rule result and populates
    // *errorMessage with the first failing rule when ok==false.
    // Side-effect free.
    static bool validate(const QJsonObject &config,
                         const ProviderCatalog *catalog,
                         QString *errorMessage = nullptr);

    // Internal detailed walker returning the result struct with every
    // violation plus any warnings. Used by the spec'd static entry
    // point (`validate(config, &err)`) and by targeted tests that want
    // all violations in one pass. Not part of the G3 public contract —
    // the static overload above is.
    ContractCheckResult validateDetailed(const QJsonObject &candidate) const;
    ContractCheckResult validateDetailed(const QJsonObject &candidate,
                                         const ProviderCatalog *catalog) const;

    // Sub-checks exposed for targeted unit tests.
    ContractCheckResult checkSchemaToken(const QJsonObject &candidate) const;
    ContractCheckResult checkTopLevelKeys(const QJsonObject &candidate) const;
    ContractCheckResult checkAgentMap(const QJsonObject &candidate) const;
    ContractCheckResult checkAgentMap(const QJsonObject &candidate,
                                      const ProviderCatalog *catalog) const;
    ContractCheckResult checkAgentEntry(const QString &agentName,
                                        const QJsonObject &entry,
                                        const QString &contextPath) const;
    ContractCheckResult checkAgentEntry(const QString &agentName,
                                        const QJsonObject &entry,
                                        const QString &contextPath,
                                        const ProviderCatalog *catalog) const;
    ContractCheckResult checkPermissionBlock(const QString &blockName,
                                             const QJsonObject &permission,
                                             const QString &contextPath) const;
    ContractCheckResult checkModelString(const QString &key,
                                         const QString &value,
                                         const QString &contextPath,
                                         const ProviderCatalog *catalog = nullptr) const;

    // Allow-lists (exposed as static so tests stay in lock-step with the
    // validator). The 40+ top-level keys come from report §4 (ConfigV1.Info
    // lines 32–166).
    static const std::vector<QString> &allowedTopLevelKeys();
    static const std::vector<QString> &allowedAgentFields();
    static const std::vector<QString> &allowedPermissionRuleKeys();
    static const std::vector<QString> &allowedPermissionActionOnlyKeys();
    static const std::vector<QString> &allowedActions();
    static const std::vector<QString> &allowedAgentModes();

    // Lightweight parseModel equivalent — report §8.3.
    // First '/' splits; provider + model must each be non-empty.
    // Returns true on success and populates providerId / modelId.
    static bool parseModel(const QString &modelId,
                           QString *providerId,
                           QString *modelIdOut,
                           QString *errorOut);
};
