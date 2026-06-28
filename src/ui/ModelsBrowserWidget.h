#pragma once

#include <QWidget>
#include <QSortFilterProxyModel>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include "models/ModelInfo.h" // for ModelsCache

namespace ModelsBrowserInternal {

// Helper used by the proxy model to bucketize output cost into coarse tiers.
inline int classifyCostTier(double outputCost)
{
    if (outputCost <= 0.0) return 0;
    if (outputCost < 1.0) return 1;  // low
    if (outputCost < 10.0) return 2; // medium
    return 3;                        // high
}

} // namespace ModelsBrowserInternal

// Column indices for the models table.
namespace ModelsBrowserColumns {
enum Column {
    Id = 0,
    DisplayName,
    InputCost,
    OutputCost,
    Capabilities,
    Provider,
    ColumnCount
};
}

// Custom item roles used by the proxy filter.
namespace ModelsBrowserRoles {
enum Role {
    ModelIdRole = Qt::UserRole + 1,
    ProviderRole,
    OutputCostRole,
    ContextWindowRole,
    CapabilitiesRole,
    PreferredProvidersRole
};
}

class QTableView;
class QStandardItemModel;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

class StorageManager;
class ModelsProxyModel;

/**
 * Models Browser tab: Fetches and displays searchable/filterable list of AI models from models.dev.
 * Supports caching, provider subscriptions, and basic connection testing.
 */
class ModelsBrowserWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * Constructor: Initializes UI and loads from cache or fetches fresh data.
     * \param storageManager Shared storage for cache and preferences.
     * \param parent Parent widget.
     */
    explicit ModelsBrowserWidget(StorageManager &storageManager, QWidget *parent = nullptr);

private slots:
    void fetchModels(); ///< Trigger network fetch or cache load.
    void onFetchFinished(); ///< Handle completed fetch reply.
    void onTableDoubleClicked(const QModelIndex &proxyIndex); ///< Copy model ID to clipboard on double-click.
    void testConnection(); ///< Test API key for selected provider.
    void manageSubscriptions(); ///< Open dialog to select preferred providers.
    void toggleSubscribedFilter(bool checked); ///< Apply/remove subscription filter.

private:
    void setupUi(); ///< Build UI layout and connect signals.
     void loadFromCacheOrFetch(); ///< Load recent cache or initiate fetch.
     void populateFromRemoteJson(const QJsonObject &root); ///< Parse and display fetched JSON.
     void clearModel(); ///< Clear table data.

    // Shared helper: populate the table and provider filters from a cached
    // ModelsCache instance. When enforceAgeLimit is true, caches older than
    // 24 hours are ignored so we can trigger a fresh network fetch.
    bool populateFromCache(const ModelsCache &cache,
                           bool enforceAgeLimit,
                           const QString &statusPrefix = QString());

    void rebuildProviderFilter(const QSet<QString> &providers); ///< Update provider combo from fetched data.

    // Helper used by both cache and network population paths
    void addModelRow(const QString &modelId,
                     const QString &displayName,
                     double inputCost,
                     double outputCost,
                     const QStringList &capabilities,
                     const QString &providerDisplay,
                     int contextWindow); ///< Add row to model with filtering data.

    QString currentProviderFilter() const; ///< Get active provider filter string.

    // Update proxy with subscription filter if enabled.
     void updateSubscriptionFilter();

private:
    StorageManager &m_storageManager;

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;

    QTableView *m_tableView = nullptr;
    QStandardItemModel *m_model = nullptr;
    ModelsProxyModel *m_proxyModel = nullptr;

    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QComboBox *m_costTierCombo = nullptr;
    QSpinBox *m_contextWindowSpin = nullptr;
    QCheckBox *m_reasoningCheck = nullptr;
    QCheckBox *m_toolUseCheck = nullptr;
    QCheckBox *m_subscribedOnlyCheck = nullptr; ///< New: Filter to subscribed providers.
    QPushButton *m_fetchButton = nullptr;
    QPushButton *m_manageSubsButton = nullptr; ///< New: Manage subscriptions.
    QPushButton *m_testConnectionButton = nullptr;

    QLabel *m_statusLabel = nullptr;

    QSet<QString> m_preferredProviders; ///< Loaded subscribed providers.
    QStringList m_allProviders; ///< For subscription dialog.
};

// Updated proxy model to support subscription filtering.
class ModelsProxyModel : public QSortFilterProxyModel
{
public:
    explicit ModelsProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setDynamicSortFilter(true);
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    void setPreferredProviders(const QSet<QString> &providers)
    {
        m_preferredProviders = providers;
        invalidateFilter();
    }

    void setSearchText(const QString &text)
    {
        m_searchText = text.trimmed().toLower();
        invalidateFilter();
    }

    void setProviderFilter(const QString &provider)
    {
        m_providerFilter = provider.trimmed();
        invalidateFilter();
    }

    void setCostTier(int tier)
    {
        m_costTier = tier;
        invalidateFilter();
    }

    void setMinContextWindow(int tokens)
    {
        m_minContextWindow = tokens;
        invalidateFilter();
    }

    void setRequireReasoning(bool enabled)
    {
        m_requireReasoning = enabled;
        invalidateFilter();
    }

    void setRequireToolUse(bool enabled)
    {
        m_requireToolUse = enabled;
        invalidateFilter();
    }

    void setSubscribedOnly(bool enabled)
    {
        m_subscribedOnly = enabled;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        const QAbstractItemModel *src = sourceModel();
        if (!src) {
            return true;
        }

        const QModelIndex idIdx = src->index(sourceRow, ModelsBrowserColumns::Id, sourceParent);
        const QModelIndex nameIdx = src->index(sourceRow, ModelsBrowserColumns::DisplayName, sourceParent);

        const QString id = src->data(idIdx, Qt::DisplayRole).toString();
        const QString name = src->data(nameIdx, Qt::DisplayRole).toString();
        const QString provider = src->data(idIdx, ModelsBrowserRoles::ProviderRole).toString();

        // Subscription filter
        if (m_subscribedOnly && !m_preferredProviders.isEmpty()) {
            if (!m_preferredProviders.contains(provider)) {
                return false;
            }
        }

        // Text search (id + display name)
        if (!m_searchText.isEmpty()) {
            const QString haystack = (id + QLatin1Char(' ') + name).toLower();
            if (!haystack.contains(m_searchText)) {
                return false;
            }
        }

        // Provider filter
        if (!m_providerFilter.isEmpty()) {
            if (provider != m_providerFilter) {
                return false;
            }
        }

        // Cost tier filter
        if (m_costTier != 0) {
            const double outputCost = src->data(idIdx, ModelsBrowserRoles::OutputCostRole).toDouble();
            const int tier = ModelsBrowserInternal::classifyCostTier(outputCost);
            if (tier != m_costTier) {
                return false;
            }
        }

        // Context window filter
        if (m_minContextWindow > 0) {
            const int contextWindow = src->data(idIdx, ModelsBrowserRoles::ContextWindowRole).toInt();
            if (contextWindow > 0 && contextWindow < m_minContextWindow) {
                return false;
            }
        }

        // Capabilities filter
        const QVariant capsVar = src->data(idIdx, ModelsBrowserRoles::CapabilitiesRole);
        const QStringList caps = capsVar.toStringList();
        if (m_requireReasoning && !caps.contains(QStringLiteral("reasoning"), Qt::CaseInsensitive)) {
            return false;
        }
        if (m_requireToolUse && !caps.contains(QStringLiteral("tool-use"), Qt::CaseInsensitive)) {
            return false;
        }

        return true;
    }

private:
    QString m_searchText;
    QString m_providerFilter;
    int m_costTier = 0;
    int m_minContextWindow = 0;
    bool m_requireReasoning = false;
    bool m_requireToolUse = false;
    bool m_subscribedOnly = false; ///< Whether to filter to preferred providers.
    QSet<QString> m_preferredProviders; ///< Subscribed providers set.
};
