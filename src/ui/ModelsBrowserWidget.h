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

// Columns in the models table
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

// Custom roles used by the proxy model for filtering
namespace ModelsBrowserRoles {
    enum Role {
        ModelIdRole = Qt::UserRole + 1,
        ProviderRole,
        OutputCostRole,
        ContextWindowRole,
        CapabilitiesRole
    };
}

// Simple proxy model that implements all filtering logic for the models table.
class ModelsProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ModelsProxyModel(QObject *parent = nullptr);

    void setSearchText(const QString &text);
    void setProviderFilter(const QString &provider);

    // 0 = all, 1 = low, 2 = medium, 3 = high
    void setCostTier(int tier);

    // Minimum required context window (tokens). 0 disables the filter.
    void setMinContextWindow(int tokens);

    void setRequireReasoning(bool enabled);
    void setRequireToolUse(bool enabled);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_searchText;
    QString m_providerFilter;
    int m_costTier = 0;
    int m_minContextWindow = 0;
    bool m_requireReasoning = false;
    bool m_requireToolUse = false;
};

// Models Browser main widget. Fetches models.dev snapshot and displays
// a searchable/filterable table of models.
class ModelsBrowserWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ModelsBrowserWidget(StorageManager &storageManager, QWidget *parent = nullptr);

private slots:
    void fetchModels();
    void onFetchFinished();
    void onTableDoubleClicked(const QModelIndex &proxyIndex);
    void testConnection();

private:
    void setupUi();
    void loadFromCacheOrFetch();
    void populateFromRemoteJson(const QJsonObject &root);
    void clearModel();

    void rebuildProviderFilter(const QSet<QString> &providers);

    // Helper used by both cache and network population paths
    void addModelRow(const QString &modelId,
                     const QString &displayName,
                     double inputCost,
                     double outputCost,
                     const QStringList &capabilities,
                     const QString &providerDisplay,
                     int contextWindow);

    QString currentProviderFilter() const;

    // Classifies a cost into low/medium/high tiers
    static int classifyCostTier(double outputCost);

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

    QPushButton *m_fetchButton = nullptr;
    QPushButton *m_testConnectionButton = nullptr;

    QLabel *m_statusLabel = nullptr;
};
