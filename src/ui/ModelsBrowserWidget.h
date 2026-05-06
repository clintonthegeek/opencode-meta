#pragma once

#include <QWidget>
#include <QSortFilterProxyModel>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

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

    // Classifies a cost into low/medium/high tiers for filtering.
    static int classifyCostTier(double outputCost);

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
    Q_OBJECT

public:
    explicit ModelsProxyModel(QObject *parent = nullptr);
    void setPreferredProviders(const QSet<QString> &providers); ///< New: Filter to subscribed only.

    void setSearchText(const QString &text);
    void setProviderFilter(const QString &provider);
    void setCostTier(int tier);
    void setMinContextWindow(int tokens);
    void setRequireReasoning(bool enabled);
    void setRequireToolUse(bool enabled);
    void setSubscribedOnly(bool enabled); ///< New: Enable/disable subscription filter.

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_searchText;
    QString m_providerFilter;
    int m_costTier = 0;
    int m_minContextWindow = 0;
    bool m_requireReasoning = false;
    bool m_requireToolUse = false;
    bool m_subscribedOnly = false; ///< New: Whether to filter to preferred providers.
    QSet<QString> m_preferredProviders; ///< Subscribed providers set.
};
