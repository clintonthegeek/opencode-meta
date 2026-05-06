#include "ui/ModelsBrowserWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QAbstractItemView>
#include <QDateTime>
#include <QSet>

#include "storage/StorageManager.h"
#include "ui/ProviderSubscriptionDialog.h"

using namespace ModelsBrowserColumns;
using namespace ModelsBrowserRoles;

// ===== ModelsProxyModel =====================================================

ModelsProxyModel::ModelsProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void ModelsProxyModel::setPreferredProviders(const QSet<QString> &providers)
{
    m_preferredProviders = providers;
    invalidateFilter();
}

void ModelsProxyModel::setSubscribedOnly(bool enabled)
{
    m_subscribedOnly = enabled;
    invalidateFilter();
}

void ModelsProxyModel::setSearchText(const QString &text)
{
    m_searchText = text.trimmed().toLower();
    invalidateFilter();
}

void ModelsProxyModel::setProviderFilter(const QString &provider)
{
    m_providerFilter = provider.trimmed();
    invalidateFilter();
}

void ModelsProxyModel::setCostTier(int tier)
{
    m_costTier = tier;
    invalidateFilter();
}

void ModelsProxyModel::setMinContextWindow(int tokens)
{
    m_minContextWindow = tokens;
    invalidateFilter();
}

void ModelsProxyModel::setRequireReasoning(bool enabled)
{
    m_requireReasoning = enabled;
    invalidateFilter();
}

void ModelsProxyModel::setRequireToolUse(bool enabled)
{
    m_requireToolUse = enabled;
    invalidateFilter();
}

bool ModelsProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *src = sourceModel();
    if (!src) {
        return true;
    }

    const QModelIndex idIdx = src->index(sourceRow, Id, sourceParent);
    const QModelIndex nameIdx = src->index(sourceRow, DisplayName, sourceParent);

    const QString id = src->data(idIdx, Qt::DisplayRole).toString();
    const QString name = src->data(nameIdx, Qt::DisplayRole).toString();
    const QString provider = src->data(idIdx, ProviderRole).toString();

    // Subscription filter (new)
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
        const double outputCost = src->data(idIdx, OutputCostRole).toDouble();
        int tier = classifyCostTier(outputCost);
        if (tier != m_costTier) {
            return false;
        }
    }

    // Context window filter
    if (m_minContextWindow > 0) {
        const int contextWindow = src->data(idIdx, ContextWindowRole).toInt();
        if (contextWindow > 0 && contextWindow < m_minContextWindow) {
            return false;
        }
    }

    // Capabilities filter
    const QVariant capsVar = src->data(idIdx, CapabilitiesRole);
    const QStringList caps = capsVar.toStringList();

    if (m_requireReasoning && !caps.contains("reasoning", Qt::CaseInsensitive)) {
        return false;
    }
    if (m_requireToolUse && !caps.contains("tool-use", Qt::CaseInsensitive)) {
        return false;
    }

    return true;
}

// ===== ModelsBrowserWidget ==================================================

ModelsBrowserWidget::ModelsBrowserWidget(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    m_networkManager = new QNetworkAccessManager(this);
    m_preferredProviders = m_storageManager.loadPreferredProviders();
    setupUi();
    loadFromCacheOrFetch();
}

void ModelsBrowserWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    // Top row: search + filters + buttons
    auto *controlsLayout = new QHBoxLayout();

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search by id or name"));

    m_providerCombo = new QComboBox(this);
    m_providerCombo->setEditable(false);
    m_providerCombo->addItem(tr("All Providers"), QString());

    m_costTierCombo = new QComboBox(this);
    m_costTierCombo->addItem(tr("All Cost Tiers"), 0);
    m_costTierCombo->addItem(tr("Low"), 1);
    m_costTierCombo->addItem(tr("Medium"), 2);
    m_costTierCombo->addItem(tr("High"), 3);

    m_contextWindowSpin = new QSpinBox(this);
    m_contextWindowSpin->setRange(0, 10'000'000);
    m_contextWindowSpin->setSingleStep(100'000);
    m_contextWindowSpin->setPrefix(tr(">= "));
    m_contextWindowSpin->setSuffix(tr(" tokens"));
    m_contextWindowSpin->setToolTip(tr("Minimum context window (0 = any)"));

    m_reasoningCheck = new QCheckBox(tr("Reasoning"), this);
    m_toolUseCheck = new QCheckBox(tr("Tool use"), this);

    m_subscribedOnlyCheck = new QCheckBox(tr("Subscribed Only"), this);
    m_subscribedOnlyCheck->setChecked(true);  // Default to filtering subscribed
    connect(m_subscribedOnlyCheck, &QCheckBox::toggled, this, &ModelsBrowserWidget::toggleSubscribedFilter);

    m_fetchButton = new QPushButton(tr("Fetch"), this);
    m_manageSubsButton = new QPushButton(tr("Manage Subscriptions"), this);
    m_testConnectionButton = new QPushButton(tr("Test Connection"), this);

    controlsLayout->addWidget(new QLabel(tr("Search:"), this));
    controlsLayout->addWidget(m_searchEdit, 1);
    controlsLayout->addWidget(new QLabel(tr("Provider:"), this));
    controlsLayout->addWidget(m_providerCombo);
    controlsLayout->addWidget(new QLabel(tr("Cost:"), this));
    controlsLayout->addWidget(m_costTierCombo);
    controlsLayout->addWidget(new QLabel(tr("Context:"), this));
    controlsLayout->addWidget(m_contextWindowSpin);
    controlsLayout->addWidget(m_reasoningCheck);
    controlsLayout->addWidget(m_toolUseCheck);
    controlsLayout->addWidget(m_subscribedOnlyCheck);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_fetchButton);
    controlsLayout->addWidget(m_manageSubsButton);
    controlsLayout->addWidget(m_testConnectionButton);

    layout->addLayout(controlsLayout);

    // Table: Sort by output cost ascending by default
    m_model = new QStandardItemModel(this);
    m_model->setColumnCount(ColumnCount);
    m_model->setHorizontalHeaderLabels({
        tr("ID"),
        tr("Display Name"),
        tr("Input Cost"),
        tr("Output Cost"),
        tr("Capabilities"),
        tr("Provider")
    });

    m_proxyModel = new ModelsProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setPreferredProviders(m_preferredProviders);
    m_proxyModel->setSubscribedOnly(true);  // Default filter

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSortingEnabled(true);  // Enable sorting
    m_tableView->sortByColumn(OutputCost, Qt::AscendingOrder);  // Default sort by cost asc
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(Id, QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(InputCost, QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(OutputCost, QHeaderView::ResizeToContents);

    layout->addWidget(m_tableView, 1);

    // Status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setText(tr("Models list is empty. Click Fetch to load from models.dev."));
    layout->addWidget(m_statusLabel);

    // Connections
    connect(m_searchEdit, &QLineEdit::textChanged, m_proxyModel, &ModelsProxyModel::setSearchText);
    connect(m_providerCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        const QString provider = (m_providerCombo->currentIndex() == 0) ? QString() : text;
        m_proxyModel->setProviderFilter(provider);
    });
    connect(m_costTierCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const int tier = m_costTierCombo->itemData(index).toInt();
        m_proxyModel->setCostTier(tier);
    });
    connect(m_contextWindowSpin, qOverload<int>(&QSpinBox::valueChanged), m_proxyModel, &ModelsProxyModel::setMinContextWindow);
    connect(m_reasoningCheck, &QCheckBox::toggled, m_proxyModel, &ModelsProxyModel::setRequireReasoning);
    connect(m_toolUseCheck, &QCheckBox::toggled, m_proxyModel, &ModelsProxyModel::setRequireToolUse);

    connect(m_fetchButton, &QPushButton::clicked, this, &ModelsBrowserWidget::fetchModels);
    connect(m_manageSubsButton, &QPushButton::clicked, this, &ModelsBrowserWidget::manageSubscriptions);
    connect(m_testConnectionButton, &QPushButton::clicked, this, &ModelsBrowserWidget::testConnection);
    connect(m_tableView, &QTableView::doubleClicked, this, &ModelsBrowserWidget::onTableDoubleClicked);
}

void ModelsBrowserWidget::clearModel()
{
    m_model->removeRows(0, m_model->rowCount());
    m_allProviders.clear();
}

void ModelsBrowserWidget::loadFromCacheOrFetch()
{
    const ModelsCache cache = m_storageManager.loadModelsCache();
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (cache.timestamp.isValid() && !cache.models.isEmpty()) {
        const qint64 ageHours = cache.timestamp.secsTo(now) / 3600;
        if (ageHours < 24) {
            m_statusLabel->setText(tr("Loaded %1 models from cache (%2 hours ago). Click Fetch to refresh.")
                                       .arg(cache.models.size())
                                       .arg(ageHours));
            clearModel();
            QSet<QString> providers;
            for (auto it = cache.models.constBegin(); it != cache.models.constEnd(); ++it) {
                const ModelInfo &info = it.value();
                QString providerDisplay = info.data.value("provider_display_name").toString();
                if (providerDisplay.isEmpty()) {
                    providerDisplay = info.data.value("provider").toString();
                }
                providers.insert(providerDisplay);
                m_allProviders.append(providerDisplay);

                const double inputCost = info.inputCost;
                const double outputCost = info.outputCost;

                int contextWindow = 0;
                const QJsonObject limitObj = info.data.value("limit").toObject();
                if (!limitObj.isEmpty()) {
                    contextWindow = limitObj.value("context").toInt();
                } else {
                    contextWindow = info.data.value("context_window").toInt();
                }

                QStringList capsList = info.capabilities.values();

                addModelRow(info.id,
                            info.displayName.isEmpty() ? info.id : info.displayName,
                            inputCost,
                            outputCost,
                            capsList,
                            providerDisplay,
                            contextWindow);
            }

            m_allProviders.removeDuplicates();
            m_allProviders.sort();
            rebuildProviderFilter(providers);
            updateSubscriptionFilter();
            return;
        }
    }

    // Cache stale or missing: fetch fresh
    fetchModels();
}

void ModelsBrowserWidget::fetchModels()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    const QUrl url("https://models.dev/api.json");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "OpenCode Meta Qt/0.1.0");
    req.setTransferTimeout(30000);  // 30s timeout

    m_statusLabel->setText(tr("Fetching models from models.dev..."));

    m_currentReply = m_networkManager->get(req);
    connect(m_currentReply, &QNetworkReply::finished, this, &ModelsBrowserWidget::onFetchFinished);
}

void ModelsBrowserWidget::onFetchFinished()
{
    if (!m_currentReply) {
        return;
    }

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_statusLabel->setText(tr("Failed to fetch models: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        m_statusLabel->setText(tr("Failed to parse models JSON: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    populateFromRemoteJson(root);
}

void ModelsBrowserWidget::populateFromRemoteJson(const QJsonObject &root)
{
    clearModel();

    ModelsCache cache;
    cache.timestamp = QDateTime::currentDateTimeUtc();  // Use current time, ignore fetched_at for simplicity

    QSet<QString> providers;

    // Parse per models-dev.md: root["providers"] is the key object
    const QJsonObject providersObj = root.value("providers").toObject();
    if (providersObj.isEmpty()) {
        m_statusLabel->setText(tr("Invalid models.dev response: No providers found."));
        return;
    }

    for (auto providerIt = providersObj.constBegin(); providerIt != providersObj.constEnd(); ++providerIt) {
        const QString providerId = providerIt.key();
        const QJsonObject providerObj = providerIt.value().toObject();
        if (providerObj.isEmpty()) continue;

        QString providerDisplay = providerObj.value("name").toString();
        if (providerDisplay.isEmpty()) providerDisplay = providerId;

        const QJsonObject modelsObj = providerObj.value("models").toObject();
        for (auto modelIt = modelsObj.constBegin(); modelIt != modelsObj.constEnd(); ++modelIt) {
            const QString modelKey = modelIt.key();
            const QJsonObject modelObj = modelIt.value().toObject();
            if (modelObj.isEmpty()) continue;

            ModelInfo info;
            info.id = modelObj.value("id").toString(modelKey);
            info.displayName = modelObj.value("name").toString(info.id);  // name is display

            // Pricing from "pricing" object
            const QJsonObject pricingObj = modelObj.value("pricing").toObject();
            info.inputCost = pricingObj.value("input").toDouble(0.0);
            info.outputCost = pricingObj.value("output").toDouble(0.0);

            // Capabilities from "capabilities" object
            const QJsonObject capsObj = modelObj.value("capabilities").toObject();
            if (capsObj.value("reasoning").toBool()) info.capabilities.insert("reasoning");
            if (capsObj.value("tool_call").toBool() || capsObj.value("tools").toBool()) info.capabilities.insert("tool-use");

            // Context from "limit.context"
            int contextWindow = modelObj.value("limit").toObject().value("context").toInt(0);

            // Raw data with provider info
            QJsonObject data = modelObj;
            data["provider"] = providerId;
            data["provider_display_name"] = providerDisplay;
            data["input_cost"] = info.inputCost;
            data["output_cost"] = info.outputCost;
            data["context_window"] = contextWindow;
            info.data = data;

            QStringList capsList = info.capabilities.values();

            addModelRow(info.id, info.displayName, info.inputCost, info.outputCost, capsList, providerDisplay, contextWindow);

            providers.insert(providerDisplay);
            m_allProviders.append(providerDisplay);

            cache.models.insert(info.id, info);
        }
    }

    m_allProviders.removeDuplicates();
    m_allProviders.sort();

    rebuildProviderFilter(providers);

    // Save cache
    m_storageManager.saveModelsCache(cache);

    const int totalModels = m_model->rowCount();
    const int visibleProviders = m_subscribedOnlyCheck->isChecked() ? m_preferredProviders.size() : providers.size();
    m_statusLabel->setText(tr("Loaded %1 models from %2 providers.").arg(totalModels).arg(visibleProviders));

    updateSubscriptionFilter();
}

void ModelsBrowserWidget::manageSubscriptions()
{
    ProviderSubscriptionDialog dlg(m_storageManager, m_allProviders, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_preferredProviders = dlg.selectedProviders();
        if (!m_preferredProviders.isEmpty()) {
            m_storageManager.savePreferredProviders(m_preferredProviders);
        }
        m_proxyModel->setPreferredProviders(m_preferredProviders);
        updateSubscriptionFilter();
        m_statusLabel->setText(tr("Subscriptions updated. Refresh to apply filters."));
    }
}

void ModelsBrowserWidget::toggleSubscribedFilter(bool checked)
{
    m_proxyModel->setSubscribedOnly(checked);
    invalidateFilter();  // Trigger re-filter
}

void ModelsBrowserWidget::updateSubscriptionFilter()
{
    m_proxyModel->setSubscribedOnly(m_subscribedOnlyCheck->isChecked());
}

// ... (rest of methods unchanged: rebuildProviderFilter, currentProviderFilter, addModelRow, onTableDoubleClicked, classifyCostTier, testConnection)
void ModelsBrowserWidget::rebuildProviderFilter(const QSet<QString> &providers)
{
    const QString current = currentProviderFilter();

    m_providerCombo->blockSignals(true);
    m_providerCombo->clear();
    m_providerCombo->addItem(tr("All Providers"), QString());

    QStringList sortedProviders = providers.values();
    sortedProviders.sort(Qt::CaseInsensitive);
    for (const QString &p : sortedProviders) {
        m_providerCombo->addItem(p, p);
    }
    m_providerCombo->blockSignals(false);

    if (!current.isEmpty()) {
        const int index = m_providerCombo->findText(current, Qt::MatchExactly);
        if (index >= 0) {
            m_providerCombo->setCurrentIndex(index);
        }
    }
}

QString ModelsBrowserWidget::currentProviderFilter() const
{
    if (m_providerCombo->currentIndex() <= 0) return QString();
    return m_providerCombo->currentText();
}

void ModelsBrowserWidget::addModelRow(const QString &modelId, const QString &displayName, double inputCost, double outputCost, const QStringList &capabilities, const QString &providerDisplay, int contextWindow)
{
    const int row = m_model->rowCount();
    m_model->insertRow(row);

    auto *idItem = new QStandardItem(modelId);
    auto *nameItem = new QStandardItem(displayName);
    auto *inputCostItem = new QStandardItem(QString::number(inputCost, 'f', 4));
    auto *outputCostItem = new QStandardItem(QString::number(outputCost, 'f', 4));
    auto *capsItem = new QStandardItem(capabilities.join(", "));
    auto *providerItem = new QStandardItem(providerDisplay);

    idItem->setData(modelId, ModelIdRole);
    idItem->setData(providerDisplay, ProviderRole);
    idItem->setData(outputCost, OutputCostRole);
    idItem->setData(contextWindow, ContextWindowRole);
    idItem->setData(capabilities, CapabilitiesRole);
    idItem->setData(providerDisplay, PreferredProvidersRole);  // For subscription filter

    m_model->setItem(row, Id, idItem);
    m_model->setItem(row, DisplayName, nameItem);
    m_model->setItem(row, InputCost, inputCostItem);
    m_model->setItem(row, OutputCost, outputCostItem);
    m_model->setItem(row, Capabilities, capsItem);
    m_model->setItem(row, Provider, providerItem);
}

void ModelsBrowserWidget::onTableDoubleClicked(const QModelIndex &proxyIndex)
{
    if (!proxyIndex.isValid()) return;

    const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const QModelIndex idIndex = m_model->index(sourceIndex.row(), Id);
    const QString id = m_model->data(idIndex, Qt::DisplayRole).toString();
    if (id.isEmpty()) return;

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(id);
    m_statusLabel->setText(tr("Copied '%1' to clipboard.").arg(id));
}

int ModelsBrowserWidget::classifyCostTier(double outputCost)
{
    if (outputCost <= 0.0) return 0;
    if (outputCost < 1.0) return 1;  // low
    if (outputCost < 10.0) return 2; // medium
    return 3; // high
}

void ModelsBrowserWidget::testConnection()
{
    // Unchanged from previous implementation
    const QString provider = currentProviderFilter();
    if (provider.isEmpty()) {
        QMessageBox::information(this, tr("Test Connection"), tr("Select a provider first."));
        return;
    }

    bool ok = false;
    const QString apiKey = QInputDialog::getText(this, tr("API Key"), tr("Enter API key for %1:").arg(provider), QLineEdit::Normal, QString(), &ok);
    if (!ok || apiKey.trimmed().isEmpty()) return;

    QUrl url;
    const QString lower = provider.toLower();
    if (lower.contains("openai")) url = QUrl("https://api.openai.com/v1/models");
    else if (lower.contains("grok") || lower.contains("x.ai") || lower.contains("xai")) url = QUrl("https://api.x.ai/v1/models");
    else if (lower.contains("302")) url = QUrl("https://api.302.ai/v1/models");
    else {
        QMessageBox::information(this, tr("Test Connection"), tr("No test endpoint for '%1'.").arg(provider));
        return;
    }

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", ("Bearer " + apiKey.trimmed().toUtf8()).constData());

    m_statusLabel->setText(tr("Testing %1...").arg(url.toString()));

    QNetworkReply *reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, provider, url]( ) {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_statusLabel->setText(tr("Test failed: %1").arg(reply->errorString()));
            QMessageBox::warning(this, tr("Test Connection"), tr("Failed: %1").arg(reply->errorString()));
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            m_statusLabel->setText(tr("Success for %1 (HTTP %2)").arg(provider).arg(status));
            QMessageBox::information(this, tr("Test Connection"), tr("Success (HTTP %1). Key valid.").arg(status));
        } else {
            m_statusLabel->setText(tr("HTTP %1 for %2").arg(status).arg(provider));
            QMessageBox::warning(this, tr("Test Connection"), tr("HTTP %1").arg(status));
        }
    });
}
