// test_models_browser.cpp
// Headless tests for models.dev cache schema, filters, and provider prefs.

#include <QTest>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardItemModel>
#include <QTemporaryDir>

#include "models/ModelInfo.h"
#include "storage/StorageManager.h"
#include "ui/ModelsBrowserWidget.h" // for ModelsBrowserColumns/Roles and ModelsProxyModel

using namespace ModelsBrowserColumns;
using namespace ModelsBrowserRoles;

class TestModelsBrowser : public QObject
{
    Q_OBJECT

private slots:
    void modelInfo_roundTrip();
    void modelsCache_roundTrip();
    void storage_preferredProviders_roundTrip();
    void storage_modelsCache_roundTrip();
    void proxy_filters_basic();
    void proxy_filters_costAndContext();
    void proxy_filters_capabilitiesAndSubscriptions();
};

void TestModelsBrowser::modelInfo_roundTrip()
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("provider/model-1");
    obj[QStringLiteral("display_name")] = QStringLiteral("Model One");
    obj[QStringLiteral("input_cost")] = 1.23;
    obj[QStringLiteral("output_cost")] = 4.56;
    obj[QStringLiteral("extra_field")] = QStringLiteral("keep-me");

    QJsonArray capsArray;
    capsArray.append(QStringLiteral("reasoning"));
    capsArray.append(QStringLiteral("tool-use"));
    obj[QStringLiteral("capabilities")] = capsArray;

    const ModelInfo info = ModelInfo::fromJson(obj);
    QCOMPARE(info.id, QStringLiteral("provider/model-1"));
    QCOMPARE(info.displayName, QStringLiteral("Model One"));
    QCOMPARE(info.inputCost, 1.23);
    QCOMPARE(info.outputCost, 4.56);
    QVERIFY(info.capabilities.contains(QStringLiteral("reasoning")));
    QVERIFY(info.capabilities.contains(QStringLiteral("tool-use")));

    const QJsonObject out = info.toJson();
    QCOMPARE(out.value(QStringLiteral("id")).toString(), QStringLiteral("provider/model-1"));
    QCOMPARE(out.value(QStringLiteral("display_name")).toString(), QStringLiteral("Model One"));
    QCOMPARE(out.value(QStringLiteral("input_cost")).toDouble(), 1.23);
    QCOMPARE(out.value(QStringLiteral("output_cost")).toDouble(), 4.56);
    QCOMPARE(out.value(QStringLiteral("extra_field")).toString(), QStringLiteral("keep-me"));

    const QJsonArray outCaps = out.value(QStringLiteral("capabilities")).toArray();
    QStringList outCapStrings;
    for (const QJsonValue &v : outCaps) {
        outCapStrings.append(v.toString());
    }
    QVERIFY(outCapStrings.contains(QStringLiteral("reasoning")));
    QVERIFY(outCapStrings.contains(QStringLiteral("tool-use")));
}

void TestModelsBrowser::modelsCache_roundTrip()
{
    ModelsCache cache;
    cache.timestamp = QDateTime::fromString(QStringLiteral("2025-01-01T00:00:00Z"), Qt::ISODate);

    ModelInfo info;
    info.id = QStringLiteral("provider/model-1");
    info.displayName = QStringLiteral("Model One");
    info.inputCost = 0.5;
    info.outputCost = 1.0;
    info.capabilities.insert(QStringLiteral("reasoning"));
    cache.models.insert(info.id, info);

    const QJsonObject obj = cache.toJson();
    const ModelsCache restored = ModelsCache::fromJson(obj);

    QCOMPARE(restored.timestamp.toString(Qt::ISODate), cache.timestamp.toString(Qt::ISODate));
    QCOMPARE(restored.models.size(), 1);
    QVERIFY(restored.models.contains(QStringLiteral("provider/model-1")));

    const ModelInfo restoredInfo = restored.models.value(QStringLiteral("provider/model-1"));
    QCOMPARE(restoredInfo.id, info.id);
    QCOMPARE(restoredInfo.displayName, info.displayName);
    QCOMPARE(restoredInfo.inputCost, info.inputCost);
    QCOMPARE(restoredInfo.outputCost, info.outputCost);
    QVERIFY(restoredInfo.capabilities.contains(QStringLiteral("reasoning")));
}

void TestModelsBrowser::storage_preferredProviders_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageManager storage(dir.path());

    QSet<QString> providers;
    providers.insert(QStringLiteral("Anthropic"));
    providers.insert(QStringLiteral("OpenAI"));

    QVERIFY(storage.savePreferredProviders(providers));

    const QSet<QString> loaded = storage.loadPreferredProviders();
    QCOMPARE(loaded, providers);
}

void TestModelsBrowser::storage_modelsCache_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageManager storage(dir.path());

    ModelsCache cache;
    cache.timestamp = QDateTime::currentDateTimeUtc();

    ModelInfo info;
    info.id = QStringLiteral("provider/model-42");
    info.displayName = QStringLiteral("The Answer Model");
    info.inputCost = 0.1;
    info.outputCost = 0.2;
    info.capabilities.insert(QStringLiteral("tool-use"));
    cache.models.insert(info.id, info);

    QVERIFY(storage.saveModelsCache(cache));

    const ModelsCache loaded = storage.loadModelsCache();
    QCOMPARE(loaded.models.size(), 1);
    QVERIFY(loaded.models.contains(QStringLiteral("provider/model-42")));
    const ModelInfo loadedInfo = loaded.models.value(QStringLiteral("provider/model-42"));
    QCOMPARE(loadedInfo.id, info.id);
    QCOMPARE(loadedInfo.displayName, info.displayName);
    QCOMPARE(loadedInfo.inputCost, info.inputCost);
    QCOMPARE(loadedInfo.outputCost, info.outputCost);
    QVERIFY(loadedInfo.capabilities.contains(QStringLiteral("tool-use")));
}

// Helper to add a row matching ModelsBrowserWidget::addModelRow so that the
// proxy model sees the same roles and column layout as the real UI.
static void addTestRow(QStandardItemModel &model,
                       const QString &modelId,
                       const QString &displayName,
                       double inputCost,
                       double outputCost,
                       const QStringList &capabilities,
                       const QString &providerDisplay,
                       int contextWindow)
{
    const int row = model.rowCount();
    model.insertRow(row);

    auto *idItem = new QStandardItem(modelId);
    auto *nameItem = new QStandardItem(displayName);
    auto *inputCostItem = new QStandardItem(QString::number(inputCost, 'f', 4));
    auto *outputCostItem = new QStandardItem(QString::number(outputCost, 'f', 4));
    auto *capsItem = new QStandardItem(capabilities.join(QStringLiteral(", ")));
    auto *providerItem = new QStandardItem(providerDisplay);

    idItem->setData(modelId, ModelIdRole);
    idItem->setData(providerDisplay, ProviderRole);
    idItem->setData(outputCost, OutputCostRole);
    idItem->setData(contextWindow, ContextWindowRole);
    idItem->setData(capabilities, CapabilitiesRole);
    idItem->setData(providerDisplay, PreferredProvidersRole);

    model.setItem(row, Id, idItem);
    model.setItem(row, DisplayName, nameItem);
    model.setItem(row, InputCost, inputCostItem);
    model.setItem(row, OutputCost, outputCostItem);
    model.setItem(row, Capabilities, capsItem);
    model.setItem(row, Provider, providerItem);
}

void TestModelsBrowser::proxy_filters_basic()
{
    QStandardItemModel src;
    src.setColumnCount(ColumnCount);

    addTestRow(src,
               QStringLiteral("openai/gpt-4"),
               QStringLiteral("GPT-4"),
               1.0,
               2.0,
               {QStringLiteral("reasoning"), QStringLiteral("tool-use")},
               QStringLiteral("OpenAI"),
               128000);

    addTestRow(src,
               QStringLiteral("anthropic/claude-haiku"),
               QStringLiteral("Claude Haiku"),
               0.25,
               0.5,
               {QStringLiteral("tool-use")},
               QStringLiteral("Anthropic"),
               200000);

    ModelsProxyModel proxy;
    proxy.setSourceModel(&src);

    QCOMPARE(proxy.rowCount(), 2);

    // Text search on id/name
    proxy.setSearchText(QStringLiteral("gpt-4"));
    QCOMPARE(proxy.rowCount(), 1);

    proxy.setSearchText(QString());
    proxy.setProviderFilter(QStringLiteral("Anthropic"));
    QCOMPARE(proxy.rowCount(), 1);

    proxy.setProviderFilter(QString());
    QCOMPARE(proxy.rowCount(), 2);
}

void TestModelsBrowser::proxy_filters_costAndContext()
{
    QStandardItemModel src;
    src.setColumnCount(ColumnCount);

    addTestRow(src,
               QStringLiteral("openai/gpt-4"),
               QStringLiteral("GPT-4"),
               1.0,
               2.0,  // medium tier
               {},
               QStringLiteral("OpenAI"),
               128000);

    addTestRow(src,
               QStringLiteral("anthropic/claude-haiku"),
               QStringLiteral("Claude Haiku"),
               0.25,
               0.5,  // low tier
               {},
               QStringLiteral("Anthropic"),
               200000);

    addTestRow(src,
               QStringLiteral("local/llama"),
               QStringLiteral("LLaMA"),
               0.05,
               0.1,  // low tier
               {},
               QStringLiteral("Local"),
               8000);

    ModelsProxyModel proxy;
    proxy.setSourceModel(&src);

    QCOMPARE(proxy.rowCount(), 3);

    // Low tier only
    proxy.setCostTier(1);
    QCOMPARE(proxy.rowCount(), 2);

    // Medium tier only
    proxy.setCostTier(2);
    QCOMPARE(proxy.rowCount(), 1);

    // Any cost, but require large context window (>= 100k)
    proxy.setCostTier(0);
    proxy.setMinContextWindow(100000);
    QCOMPARE(proxy.rowCount(), 2);

    // Very large context requirement filters everything
    proxy.setMinContextWindow(5000000);
    QCOMPARE(proxy.rowCount(), 0);
}

void TestModelsBrowser::proxy_filters_capabilitiesAndSubscriptions()
{
    QStandardItemModel src;
    src.setColumnCount(ColumnCount);

    addTestRow(src,
               QStringLiteral("openai/gpt-4"),
               QStringLiteral("GPT-4"),
               1.0,
               2.0,
               {QStringLiteral("reasoning"), QStringLiteral("tool-use")},
               QStringLiteral("OpenAI"),
               128000);

    addTestRow(src,
               QStringLiteral("anthropic/claude-haiku"),
               QStringLiteral("Claude Haiku"),
               0.25,
               0.5,
               {QStringLiteral("tool-use")},
               QStringLiteral("Anthropic"),
               200000);

    addTestRow(src,
               QStringLiteral("local/llama"),
               QStringLiteral("LLaMA"),
               0.05,
               0.1,
               {},
               QStringLiteral("Local"),
               8000);

    ModelsProxyModel proxy;
    proxy.setSourceModel(&src);

    QCOMPARE(proxy.rowCount(), 3);

    // Require reasoning capability
    proxy.setRequireReasoning(true);
    QCOMPARE(proxy.rowCount(), 1);

    // Require tool-use capability only (no reasoning requirement)
    proxy.setRequireReasoning(false);
    proxy.setRequireToolUse(true);
    QCOMPARE(proxy.rowCount(), 2);

    // Subscription filter: only Anthropic
    proxy.setRequireToolUse(false);
    QSet<QString> preferred;
    preferred.insert(QStringLiteral("Anthropic"));
    proxy.setPreferredProviders(preferred);
    proxy.setSubscribedOnly(true);
    QCOMPARE(proxy.rowCount(), 1);
}

QTEST_MAIN(TestModelsBrowser)
#include "test_models_browser.moc"
