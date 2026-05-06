#include "ModelsBrowserWidget.h"

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

using namespace ModelsBrowserColumns;
using namespace ModelsBrowserRoles;

// ===== ModelsProxyModel =====================================================

ModelsProxyModel::ModelsProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
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

    // Text search (id + display name)
    if (!m_searchText.isEmpty()) {
        const QString haystack = (id + QLatin1Char(' ') + name).toLower();
        if (!haystack.contains(m_searchText)) {
            return false;
        }
    }

    // Provider filter
    if (!m_providerFilter.isEmpty()) {
        const QString provider = src->data(idIdx, ProviderRole).toString();
        if (provider != m_providerFilter) {
            return false;
        }
    }

    // Cost tier filter
    if (m_costTier != 0) {
        const double outputCost = src->data(idIdx, OutputCostRole).toDouble();
        int tier = 0;
        if (outputCost > 0.0) {
            if (outputCost < 1.0) {
                tier = 1; // low
            } else if (outputCost < 10.0) {
                tier = 2; // medium
            } else {
                tier = 3; // high
            }
        }
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

    if (m_requireReasoning && !caps.contains(QStringLiteral("reasoning"), Qt::CaseInsensitive)) {
        return false;
    }
    if (m_requireToolUse && !caps.contains(QStringLiteral("tool-use"), Qt::CaseInsensitive)) {
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

    m_fetchButton = new QPushButton(tr("Fetch"), this);
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
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_fetchButton);
    controlsLayout->addWidget(m_testConnectionButton);

    layout->addLayout(controlsLayout);

    // Table
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

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
    connect(m_testConnectionButton, &QPushButton::clicked, this, &ModelsBrowserWidget::testConnection);
    connect(m_tableView, &QTableView::doubleClicked, this, &ModelsBrowserWidget::onTableDoubleClicked);
}

void ModelsBrowserWidget::clearModel()
{
    m_model->removeRows(0, m_model->rowCount());
}

void ModelsBrowserWidget::loadFromCacheOrFetch()
{
    const ModelsCache cache = m_storageManager.loadModelsCache();
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (cache.timestamp.isValid()) {
        const qint64 ageSecs = cache.timestamp.secsTo(now);
        if (ageSecs >= 0 && ageSecs < 24 * 60 * 60 && !cache.models.isEmpty()) {
            m_statusLabel->setText(tr("Loaded models from cache (updated %1)")
                                       .arg(cache.timestamp.toLocalTime().toString(Qt::ISODate)));
            clearModel();
            QSet<QString> providers;
            for (auto it = cache.models.constBegin(); it != cache.models.constEnd(); ++it) {
                const ModelInfo &info = it.value();

                // Provider display name (if present) falls back to provider id.
                QString providerDisplay = info.data.value(QStringLiteral("provider_display_name")).toString();
                if (providerDisplay.isEmpty()) {
                    providerDisplay = info.data.value(QStringLiteral("provider")).toString();
                }

                const double inputCost = info.inputCost;
                const double outputCost = info.outputCost;

                int contextWindow = 0;
                const QJsonObject limitObj = info.data.value(QStringLiteral("limit")).toObject();
                if (!limitObj.isEmpty()) {
                    contextWindow = limitObj.value(QStringLiteral("context")).toInt();
                } else {
                    contextWindow = info.data.value(QStringLiteral("context_window")).toInt();
                }

                QStringList capsList = info.capabilities.values();

                addModelRow(info.id,
                            info.displayName.isEmpty() ? info.id : info.displayName,
                            inputCost,
                            outputCost,
                            capsList,
                            providerDisplay,
                            contextWindow);

                if (!providerDisplay.isEmpty()) {
                    providers.insert(providerDisplay);
                }
            }

            rebuildProviderFilter(providers);
            return;
        }
    }

    // Cache missing or stale: fetch from network
    fetchModels();
}

void ModelsBrowserWidget::fetchModels()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    const QUrl url(QStringLiteral("https://models.dev/api.json"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("opencode-meta-qt"));

    m_statusLabel->setText(tr("Fetching models from %1 ...").arg(url.toString()));

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
    cache.timestamp = QDateTime::currentDateTimeUtc();

    QSet<QString> providers;

    // The models.dev snapshot is a map of provider id -> provider object,
    // each containing a "models" object.
    const QJsonObject providersObj = root.value(QStringLiteral("providers")).toObject();
    for (auto providerIt = providersObj.begin(); providerIt != providersObj.end(); ++providerIt) {
        if (!providerIt.value().isObject()) {
            continue;
        }

        const QString providerId = providerIt.key();
        const QJsonObject providerObj = providerIt.value().toObject();

        QString providerDisplay = providerObj.value(QStringLiteral("name")).toString();
        if (providerDisplay.isEmpty()) {
            providerDisplay = providerId;
        }

        const QJsonObject modelsObj = providerObj.value(QStringLiteral("models")).toObject();
        for (auto modelIt = modelsObj.begin(); modelIt != modelsObj.end(); ++modelIt) {
            if (!modelIt.value().isObject()) {
                continue;
            }

            const QString modelKey = modelIt.key();
            const QJsonObject modelObj = modelIt.value().toObject();

            ModelInfo info;

            // Basic identifiers
            info.id = modelObj.value(QStringLiteral("id")).toString(modelKey);
            // Prefer display_name, fall back to name, then id.
            info.displayName = modelObj.value(QStringLiteral("display_name"))
                                   .toString(modelObj.value(QStringLiteral("name"))
                                                 .toString(info.id));

            // Pricing: best-effort mapping into input/output cost fields.
            const QJsonObject pricingObj = modelObj.value(QStringLiteral("pricing")).toObject();
            double inputCost = 0.0;
            double outputCost = 0.0;

            if (!pricingObj.isEmpty()) {
                const QJsonValue in = pricingObj.value(QStringLiteral("input"));
                const QJsonValue out = pricingObj.value(QStringLiteral("output"));
                const QJsonValue prompt = pricingObj.value(QStringLiteral("prompt"));
                const QJsonValue total = pricingObj.value(QStringLiteral("total"));
                const QJsonValue cost = pricingObj.value(QStringLiteral("cost"));

                if (in.isDouble()) {
                    inputCost = in.toDouble();
                } else if (prompt.isDouble()) {
                    inputCost = prompt.toDouble();
                } else if (total.isDouble()) {
                    inputCost = total.toDouble();
                } else if (cost.isDouble()) {
                    inputCost = cost.toDouble();
                }

                if (out.isDouble()) {
                    outputCost = out.toDouble();
                } else {
                    // Fall back to the single cost if no dedicated output price.
                    outputCost = inputCost;
                }
            }

            info.inputCost = inputCost;
            info.outputCost = outputCost;

            // Capabilities: map models.dev capability flags into simple tags.
            const QJsonObject capsObj = modelObj.value(QStringLiteral("capabilities")).toObject();

            const bool reasoning = capsObj.value(QStringLiteral("reasoning")).toBool(false);
            if (reasoning) {
                info.capabilities.insert(QStringLiteral("reasoning"));
            }

            const char *toolKeys[] = {"tools", "tool_use", "tool_call", "function_call"};
            for (const char *key : toolKeys) {
                const QJsonValue v = capsObj.value(QLatin1String(key));
                if (v.isBool() && v.toBool()) {
                    info.capabilities.insert(QStringLiteral("tool-use"));
                    break;
                }
            }

            // Preserve the raw model payload, augmented with provider metadata
            QJsonObject data = modelObj;
            data.insert(QStringLiteral("provider"), providerId);
            data.insert(QStringLiteral("provider_display_name"), providerDisplay);
            data.insert(QStringLiteral("input_cost"), info.inputCost);
            data.insert(QStringLiteral("output_cost"), info.outputCost);

            // Best-effort context window extraction from capabilities.
            int contextWindow = 0;
            const char *ctxKeys[] = {"max_context", "context", "context_length", "context_window"};
            for (const char *key : ctxKeys) {
                const QJsonValue v = capsObj.value(QLatin1String(key));
                if (v.isDouble()) {
                    contextWindow = v.toInt();
                    break;
                }
            }
            if (contextWindow > 0) {
                data.insert(QStringLiteral("context_window"), contextWindow);
            }

            if (!info.capabilities.isEmpty()) {
                QJsonArray capsArray;
                const QStringList capsList = info.capabilities.values();
                for (const QString &cap : capsList) {
                    capsArray.append(cap);
                }
                data.insert(QStringLiteral("capabilities"), capsArray);
            }

            info.data = data;

            const QStringList capsList = info.capabilities.values();

            addModelRow(info.id,
                        info.displayName,
                        info.inputCost,
                        info.outputCost,
                        capsList,
                        providerDisplay,
                        contextWindow);

            providers.insert(providerDisplay);

            cache.models.insert(info.id, info);
        }
    }

    rebuildProviderFilter(providers);

    // Persist cache to disk
    m_storageManager.saveModelsCache(cache);

    m_statusLabel->setText(tr("Loaded %1 providers, %2 models from models.dev")
                               .arg(providers.size())
                               .arg(m_model->rowCount()));
}

void ModelsBrowserWidget::rebuildProviderFilter(const QSet<QString> &providers)
{
    const QString current = currentProviderFilter();

    m_providerCombo->blockSignals(true);
    m_providerCombo->clear();
    m_providerCombo->addItem(tr("All Providers"), QString());

    QStringList sorted = providers.values();
    sorted.sort(Qt::CaseInsensitive);
    for (const QString &p : sorted) {
        m_providerCombo->addItem(p, p);
    }
    m_providerCombo->blockSignals(false);

    // Restore previous selection if possible
    if (!current.isEmpty()) {
        const int index = m_providerCombo->findText(current, Qt::MatchExactly);
        if (index >= 0) {
            m_providerCombo->setCurrentIndex(index);
        } else {
            m_providerCombo->setCurrentIndex(0);
        }
    } else {
        m_providerCombo->setCurrentIndex(0);
    }
}

QString ModelsBrowserWidget::currentProviderFilter() const
{
    if (!m_providerCombo) {
        return QString();
    }
    if (m_providerCombo->currentIndex() <= 0) {
        return QString();
    }
    return m_providerCombo->currentText();
}

void ModelsBrowserWidget::addModelRow(const QString &modelId,
                                      const QString &displayName,
                                      double inputCost,
                                      double outputCost,
                                      const QStringList &capabilities,
                                      const QString &providerDisplay,
                                      int contextWindow)
{
    const int row = m_model->rowCount();
    m_model->insertRow(row);

    auto *idItem = new QStandardItem(modelId);
    auto *nameItem = new QStandardItem(displayName);
    auto *inputCostItem = new QStandardItem(QString::number(inputCost));
    auto *outputCostItem = new QStandardItem(QString::number(outputCost));
    auto *capsItem = new QStandardItem(capabilities.join(QStringLiteral(", ")));
    auto *providerItem = new QStandardItem(providerDisplay);

    // Store data for filtering roles on the id item
    idItem->setData(modelId, ModelIdRole);
    idItem->setData(providerDisplay, ProviderRole);
    idItem->setData(outputCost, OutputCostRole);
    idItem->setData(contextWindow, ContextWindowRole);
    idItem->setData(capabilities, CapabilitiesRole);

    m_model->setItem(row, Id, idItem);
    m_model->setItem(row, DisplayName, nameItem);
    m_model->setItem(row, InputCost, inputCostItem);
    m_model->setItem(row, OutputCost, outputCostItem);
    m_model->setItem(row, Capabilities, capsItem);
    m_model->setItem(row, Provider, providerItem);
}

void ModelsBrowserWidget::onTableDoubleClicked(const QModelIndex &proxyIndex)
{
    if (!proxyIndex.isValid()) {
        return;
    }

    const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const QModelIndex idIndex = m_model->index(sourceIndex.row(), Id);
    const QString id = m_model->data(idIndex, Qt::DisplayRole).toString();
    if (id.isEmpty()) {
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(id);
    m_statusLabel->setText(tr("Copied model id to clipboard: %1").arg(id));
}

int ModelsBrowserWidget::classifyCostTier(double outputCost)
{
    if (outputCost <= 0.0) {
        return 0;
    }
    if (outputCost < 1.0) {
        return 1; // low
    }
    if (outputCost < 10.0) {
        return 2; // medium
    }
    return 3; // high
}

void ModelsBrowserWidget::testConnection()
{
    const QString provider = currentProviderFilter();
    if (provider.isEmpty()) {
        QMessageBox::information(this, tr("Test Connection"), tr("Select a provider first."));
        return;
    }

    bool ok = false;
    const QString apiKey = QInputDialog::getText(this,
                                                 tr("API Key"),
                                                 tr("Enter API key for %1:").arg(provider),
                                                 QLineEdit::Normal,
                                                 QString(),
                                                 &ok);
    if (!ok || apiKey.trimmed().isEmpty()) {
        return;
    }

    QUrl url;

    // Very small set of known providers, others are stubs.
    const QString lower = provider.toLower();
    if (lower.contains(QStringLiteral("openai"))) {
        url = QUrl(QStringLiteral("https://api.openai.com/v1/models"));
    } else if (lower.contains(QStringLiteral("grok")) || lower.contains(QStringLiteral("x.ai")) || lower.contains(QStringLiteral("xai"))) {
        url = QUrl(QStringLiteral("https://api.x.ai/v1/models"));
    } else if (lower.contains(QStringLiteral("302"))) {
        url = QUrl(QStringLiteral("https://api.302.ai/v1/models"));
    } else {
        QMessageBox::information(this,
                                 tr("Test Connection"),
                                 tr("No test endpoint configured for provider '%1'. This is a placeholder.").arg(provider));
        return;
    }

    QNetworkRequest req(url);
    const QByteArray authHeader = QByteArrayLiteral("Bearer ") + apiKey.trimmed().toUtf8();
    req.setRawHeader("Authorization", authHeader);

    m_statusLabel->setText(tr("Testing connection to %1 ...").arg(url.toString()));

    QNetworkReply *reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, provider, url]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            m_statusLabel->setText(tr("Connection test failed for %1: %2")
                                       .arg(provider, reply->errorString()));
            QMessageBox::warning(this,
                                 tr("Test Connection"),
                                 tr("Request to %1 failed: %2").arg(url.toString(), reply->errorString()));
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            m_statusLabel->setText(tr("Connection test succeeded for %1 (HTTP %2)")
                                       .arg(provider)
                                       .arg(status));
            QMessageBox::information(this,
                                     tr("Test Connection"),
                                     tr("Successfully reached %1 (HTTP %2).\nAPI key appears to be valid.")
                                         .arg(url.toString())
                                         .arg(status));
        } else {
            m_statusLabel->setText(tr("Connection test returned HTTP %1 for %2")
                                       .arg(status)
                                       .arg(provider));
            QMessageBox::warning(this,
                                 tr("Test Connection"),
                                 tr("Request to %1 returned HTTP %2.")
                                     .arg(url.toString())
                                     .arg(status));
        }
    });
}
