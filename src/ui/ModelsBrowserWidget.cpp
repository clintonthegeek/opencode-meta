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
#include <QDebug>
#include "storage/StorageManager.h"
#include "ui/ProviderSubscriptionDialog.h"
using namespace ModelsBrowserColumns;
using namespace ModelsBrowserRoles;
// ===== ModelsBrowserWidget ==================================================
ModelsBrowserWidget::ModelsBrowserWidget(StorageManager &storageManager,
                                         QWidget *parent,
                                         bool pickerMode)
    : QWidget(parent)
    , m_storageManager(storageManager)
    , m_pickerMode(pickerMode)
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
    m_statusLabel->setText(tr("Models list is empty. Click Fetch to run `opencode models --refresh`."));
    layout->addWidget(m_statusLabel);

    // Picker-mode controls: explicit OK/Cancel buttons that embedders can
    // connect to in dialogs or other views.
    if (m_pickerMode) {
        auto *buttonsLayout = new QHBoxLayout();
        buttonsLayout->addStretch(1);

        m_acceptButton = new QPushButton(tr("OK"), this);
        m_cancelButton = new QPushButton(tr("Cancel"), this);

        buttonsLayout->addWidget(m_acceptButton);
        buttonsLayout->addWidget(m_cancelButton);

        layout->addLayout(buttonsLayout);

        connect(m_acceptButton, &QPushButton::clicked, this, [this]() {
            const QString id = selectedModelId();
            if (id.isEmpty()) {
                if (m_statusLabel) {
                    m_statusLabel->setText(tr("Select a model before accepting."));
                }
                return;
            }
            emit modelAccepted(id);
        });

        connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
            emit selectionCanceled();
        });
    }
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
    // Phase G1: the live `<Global.Path.cache>/models.json` (opencode-
    // managed) is the authoritative source. Try it first; if it's empty
    // (or the user has never run `opencode models --refresh`), fall
    // back to the local `~/.opencode-meta/models-cache.json` snapshot.
    if (populateFromLiveCatalog(/*forceRefresh=*/false) > 0) {
        return;
    }
    const ModelsCache cache = m_storageManager.loadModelsCache();
    if (populateFromCache(cache, /*enforceAgeLimit=*/true)) {
        return;
    }
    // Last resort: refresh the opencode catalog, then read it again.
    if (populateFromLiveCatalog(/*forceRefresh=*/true) > 0) {
        return;
    }
    m_statusLabel->setText(tr(
        "Live catalog empty. Click Fetch to run `opencode models --refresh` "
        "or install opencode so it can populate <Global.Path.cache>/models.json."));
}
void ModelsBrowserWidget::fetchModels()
{
    // Phase G1: "Fetch" now means "shell out to opencode models --refresh"
    // rather than a direct HTTP GET against models.dev/api.json. The
    // direct fetch path is removed entirely — see
    // docs/PARADIGM.md §5.6 and OPENCODE-CONFIG-INTROSPECTION.md §12.2
    // item 1.
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_statusLabel->setText(tr("Running `opencode models --refresh`..."));
    const int count = populateFromLiveCatalog(/*forceRefresh=*/true,
                                             tr("Refresh:"));
    if (count <= 0) {
        // Fall back to offline cache so the user still has something to
        // browse while the opencode cache is missing.
        const ModelsCache cache = m_storageManager.loadModelsCache();
        (void)populateFromCache(cache, /*enforceAgeLimit=*/false,
                                tr("Live catalog unreachable; showing cached models."));
    }
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
        // Network error: try to fall back to whatever cache we have, even if
        // it is older than the usual freshness window. If there is no cache,
        // surface a clear error message.
        const ModelsCache cache = m_storageManager.loadModelsCache();
        if (!populateFromCache(cache, /*enforceAgeLimit=*/false,
                               tr("Network error; showing cached models."))) {
            m_statusLabel->setText(tr("Failed to load models - check network."));
        }
        return;
    }
    const QByteArray data = reply->readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Parse/format error: again, fall back to any existing cache.
        const ModelsCache cache = m_storageManager.loadModelsCache();
        if (!populateFromCache(cache, /*enforceAgeLimit=*/false,
                               tr("Invalid response; showing cached models."))) {
            m_statusLabel->setText(tr("Failed to load models - invalid response."));
        }
        return;
    }
    const QJsonObject root = doc.object();
    qDebug() << "Parsed <Global.Path.cache>/models.json root keys:" << root.keys().size();
    if (root.keys().size() <= 0) {
        const ModelsCache cache = m_storageManager.loadModelsCache();
        if (!populateFromCache(cache, /*enforceAgeLimit=*/false,
                               tr("Empty response; showing cached models."))) {
            m_statusLabel->setText(tr("Failed to load models - empty response."));
        }
        return;
    }
    populateFromRemoteJson(root);
}
int ModelsBrowserWidget::populateFromLiveCatalog(bool forceRefresh,
                                                  const QString &statusPrefix)
{
    if (forceRefresh) {
        // Refresh runs `opencode models --refresh`. We deliberately do
        // not abort-on-empty-cache: a successful refresh against a TTL
        // < 5min short-circuits the network refetch using opencode's own
        // schedule (report §8.1: 60-min auto refresh).
        m_catalog.refreshFromCli();
    }
    if (!m_catalog.loadFromCache()) {
        return 0;
    }
    if (m_catalog.providerCount() <= 0 || m_catalog.modelCount() <= 0) {
        return 0;
    }
    clearModel();

    // Walk the parsed catalog and emit one row per (provider, model).
    // The shape mirrors the models.dev/api.json schema (provider id →
    // models map; per-model `name`, `cost`, `limit.context`,
    // `tool_call`/`reasoning`/`attachment`).
    QSet<QString> providerFilterSet;
    const QStringList providers = m_catalog.providerIDs();
    int totalRows = 0;
    for (const QString &providerID : providers) {
        const QJsonValue pval = m_catalog.providerObject(providerID);
        if (!pval.isObject()) {
            continue;
        }
        const QJsonObject pobj = pval.toObject();
        const QString providerDisplay = pobj.value(QStringLiteral("name")).toString(providerID);
        const QStringList models = m_catalog.modelIDs(providerID);
        for (const QString &modelID : models) {
            const QJsonValue mval = pobj.value(QStringLiteral("models")).toObject().value(modelID);
            if (!mval.isObject()) {
                continue;
            }
            const QJsonObject mobj = mval.toObject();
            const QString displayName = mobj.value(QStringLiteral("name")).toString(modelID);
            const QJsonObject costObj = mobj.value(QStringLiteral("cost")).toObject();
            const double inputCost = costObj.value(QStringLiteral("input")).toDouble(0.0);
            const double outputCost = costObj.value(QStringLiteral("output")).toDouble(0.0);
            const int contextWindow = mobj.value(QStringLiteral("limit")).toObject().value(QStringLiteral("context")).toInt(0);
            QStringList caps;
            if (mobj.value(QStringLiteral("reasoning")).toBool()) {
                caps.append(QStringLiteral("reasoning"));
            }
            if (mobj.value(QStringLiteral("tool_call")).toBool() || mobj.value(QStringLiteral("tools")).toBool()) {
                caps.append(QStringLiteral("tool-use"));
            }
            if (mobj.value(QStringLiteral("attachment")).toBool()) {
                caps.append(QStringLiteral("attachment"));
            }
            addModelRow(QStringLiteral("%1/%2").arg(providerID, modelID),
                        displayName,
                        inputCost,
                        outputCost,
                        caps,
                        providerDisplay,
                        contextWindow);
            providerFilterSet.insert(providerDisplay);
            m_allProviders.append(providerDisplay);
            ++totalRows;
        }
    }
    m_allProviders.removeDuplicates();
    m_allProviders.sort();
    rebuildProviderFilter(providerFilterSet);
    updateSubscriptionFilter();

    const int visible = m_proxyModel ? m_proxyModel->rowCount() : totalRows;
    if (m_statusLabel) {
        const QString prefix = statusPrefix.isEmpty() ? QString() : statusPrefix + QLatin1Char(' ');
        m_statusLabel->setText(tr(
            "%1Loaded %2 models from %3 providers (%4 total in live catalog). %5 visible after filters.")
            .arg(prefix)
            .arg(totalRows)
            .arg(m_catalog.providerCount())
            .arg(m_catalog.modelCount())
            .arg(visible));
    }
    return totalRows;
}

void ModelsBrowserWidget::populateFromRemoteJson(const QJsonObject &root)
{
    clearModel();
    ModelsCache cache;
    cache.timestamp = QDateTime::currentDateTimeUtc();  // Use current time, ignore fetched_at for simplicity
    QSet<QString> providers;
    if (root.isEmpty()) {
        qDebug() << "Empty/invalid root";
        m_statusLabel->setText(tr("Failed to load - invalid JSON"));
        return;
    }
    // Per models.dev schema (loaded into <Global.Path.cache>/models.json
    // by opencode models --refresh, see ProviderCatalog and introspection
    // report §8.1/§8.4): root keys are provider ids (e.g. { "openai": { ... } })
    for (auto providerIt = root.constBegin(); providerIt != root.constEnd(); ++providerIt) {
        const QString providerId = providerIt.key();
        const QJsonObject providerObj = providerIt.value().toObject();
        if (providerObj.isEmpty()) continue;
        QString providerDisplay = providerObj.value("name").toString(providerId);
        const QJsonObject modelsObj = providerObj.value("models").toObject();
        for (auto modelIt = modelsObj.constBegin(); modelIt != modelsObj.constEnd(); ++modelIt) {
            const QString modelKey = modelIt.key();
            const QJsonObject modelObj = modelIt.value().toObject();
            if (modelObj.isEmpty()) continue;
            ModelInfo info;
            info.id = modelObj.value("id").toString(modelKey);
            info.displayName = modelObj.value("name").toString(info.id);  // name is display
            // Pricing from "cost" object (per models.dev schema loaded
            // into <Global.Path.cache>/models.json)
            const QJsonObject costObj = modelObj.value("cost").toObject();
            info.inputCost = costObj.value("input").toDouble(0.0);
            info.outputCost = costObj.value("output").toDouble(0.0);
            // Capabilities from top-level booleans
            if (modelObj.value("reasoning").toBool()) {
                info.capabilities.insert("reasoning");
            }
            if (modelObj.value("tool_call").toBool() || modelObj.value("tools").toBool()) {
                info.capabilities.insert("tool-use");
            }
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
    // Apply subscription filter after population
    updateSubscriptionFilter();
    const int totalModels = m_model->rowCount();
    const int providerCount = providers.size();
    const int visibleModels = m_proxyModel ? m_proxyModel->rowCount() : totalModels;
    m_statusLabel->setText(tr("Loaded %1 models from %2 providers. %3 visible after filters.")
                               .arg(totalModels)
                               .arg(providerCount)
                               .arg(visibleModels));
}

bool ModelsBrowserWidget::populateFromCache(const ModelsCache &cache,
                                            bool enforceAgeLimit,
                                            const QString &statusPrefix)
{
    if (!cache.timestamp.isValid() || cache.models.isEmpty()) {
        return false;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 ageHours = cache.timestamp.secsTo(now) / 3600;
    if (enforceAgeLimit && ageHours >= 24) {
        return false;
    }

    clearModel();

    QSet<QString> providers;
    const auto keys = cache.models.keys();
    for (const QString &key : keys) {
        const ModelInfo &info = cache.models.value(key);

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

    const int totalModels = cache.models.size();
    const int providerCount = providers.size();
    const int visibleModels = m_proxyModel ? m_proxyModel->rowCount() : totalModels;

    if (!m_statusLabel) {
        return true;
    }

    if (statusPrefix.isEmpty()) {
        m_statusLabel->setText(tr("Loaded %1 models from %2 providers (%3 hours ago). %4 visible after filters.")
                                   .arg(totalModels)
                                   .arg(providerCount)
                                   .arg(ageHours)
                                   .arg(visibleModels));
    } else {
        m_statusLabel->setText(tr("%1 Loaded %2 models from %3 providers (%4 hours ago). %5 visible after filters.")
                                   .arg(statusPrefix)
                                   .arg(totalModels)
                                   .arg(providerCount)
                                   .arg(ageHours)
                                   .arg(visibleModels));
    }

    return true;
}

void ModelsBrowserWidget::manageSubscriptions()
{
    ProviderSubscriptionDialog dlg(m_storageManager, m_allProviders, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_preferredProviders = dlg.selectedProviders();
        m_storageManager.savePreferredProviders(m_preferredProviders);
        m_proxyModel->setPreferredProviders(m_preferredProviders);
        updateSubscriptionFilter();
        m_statusLabel->setText(tr("Subscriptions updated. Filters apply immediately."));
    }
}

void ModelsBrowserWidget::toggleSubscribedFilter(bool checked)
{
    if (m_proxyModel) {
        m_proxyModel->setSubscribedOnly(checked);
    }
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

    // In picker mode, treat double-click as an accept action on the
    // current row. In browser mode, keep the existing behavior of
    // copying the id to the clipboard.
    if (m_pickerMode) {
        emit modelAccepted(id);
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(id);
    m_statusLabel->setText(tr("Copied '%1' to clipboard.").arg(id));
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

QString ModelsBrowserWidget::selectedModelId() const
{
    if (!m_tableView || !m_proxyModel || !m_model) {
        return QString();
    }

    QItemSelectionModel *sel = m_tableView->selectionModel();
    if (!sel) {
        return QString();
    }

    const QModelIndexList rows = sel->selectedRows(Id);
    if (rows.isEmpty()) {
        return QString();
    }

    const QModelIndex proxyIndex = rows.first();
    const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const QModelIndex idIndex = m_model->index(sourceIndex.row(), Id);
    return m_model->data(idIndex, Qt::DisplayRole).toString();
}
