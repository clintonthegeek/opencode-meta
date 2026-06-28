// test_opencode_schema_adapter.cpp
// Qt Test harness for OpencodeSchemaAdapter (M1)

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryFile>

#include "adapter/OpencodeSchemaAdapter.h"
#include "models/Template.h"
#include "models/Profile.h"

class TestOpencodeSchemaAdapter : public QObject
{
    Q_OBJECT

private slots:
    void testRoundTripPreservesUnknownFields();
    void testDefaultAgentMustBePrimaryOrAll();
    void testLoadRealWorldConfig();
    void testNestedAgentUnknownFieldsPreserved();
    void testPermissionObjectPreservedWithoutLegacyFlags();
};

void TestOpencodeSchemaAdapter::testRoundTripPreservesUnknownFields()
{
    QJsonObject input;
    input["name"] = "test";
    input["agent"] = QJsonObject{{"coder", QJsonObject{{"mode", "primary"}, {"prompt", "hello"}}}};
    input["default"] = "coder";
    input["future_policy"] = QJsonObject{{"foo", 123}}; // unknown field

    Template tpl = OpencodeSchemaAdapter::loadTemplate(input);
    QJsonObject out = OpencodeSchemaAdapter::saveTemplate(tpl);

    QVERIFY(out.contains("future_policy"));
    QCOMPARE(out["future_policy"].toObject()["foo"].toInt(), 123);
}

void TestOpencodeSchemaAdapter::testDefaultAgentMustBePrimaryOrAll()
{
    Template tpl;
    AgentDef sub;
    sub.mode = AgentDef::Mode::Subagent;
    tpl.agents.insert("helper", sub);
    tpl.defaultAgent = "helper";

    auto err = OpencodeSchemaAdapter::validate(tpl);
    QVERIFY(err.has_value());
    QVERIFY(err->contains("Primary or All"));
}

void TestOpencodeSchemaAdapter::testLoadRealWorldConfig()
{
    // More realistic opencode.json-style config, similar to opencode-meta/opencode.json
    QJsonObject real;
    real["$schema"] = "https://opencode.ai/config.json";
    real["model"] = "xai/grok-4-fast";
    real["small_model"] = "openai/gpt-5.4-mini";

    QJsonObject agents;

    QJsonObject build;
    build["mode"] = "primary";
    build["model"] = "xai/grok-4-fast";
    build["description"] = "Project orchestrator";
    build["prompt"] = "{file:./prompts/orchestrator.md}";
    build["edit"] = "ask";
    build["bash"] = "ask";
    build["task"] = "allow";
    build["read"] = "allow";
    build["grep"] = "ask";
    agents["build"] = build;

    QJsonObject explorer;
    explorer["mode"] = "subagent";
    explorer["model"] = "openai/gpt-5.4-mini";
    explorer["description"] = "Fast codebase explorer";
    explorer["edit"] = "deny";
    explorer["bash"] = "allow";
    agents["explorer"] = explorer;

    real["agent"] = agents;
    real["default_agent"] = "build";
    real["extra_section"] = QJsonObject{{"x", true}};

    Template tpl = OpencodeSchemaAdapter::loadTemplate(real);
    QCOMPARE(tpl.name, QString()); // name is not set in this config
    QVERIFY(tpl.agents.contains("build"));
    QVERIFY(tpl.agents.contains("explorer"));
    QCOMPARE(tpl.defaultAgent, QString("build"));
    QVERIFY(tpl.metadata.contains("_raw_opencode"));

    auto err = OpencodeSchemaAdapter::validate(tpl);
    QVERIFY(!err.has_value());

    QJsonObject out = OpencodeSchemaAdapter::saveTemplate(tpl);
    QVERIFY(out.contains("extra_section"));
    QCOMPARE(out.value("default_agent").toString(), QString("build"));
    const QJsonObject outAgents = out.value("agent").toObject();
    QVERIFY(outAgents.contains("build"));
    QVERIFY(outAgents.contains("explorer"));
}

void TestOpencodeSchemaAdapter::testNestedAgentUnknownFieldsPreserved()
{
    // Agent-level nested unknown fields should survive a load/save round-trip.
    QJsonObject root;
    root["name"] = "nested-test";

    QJsonObject coder;
    coder["mode"] = "primary";
    coder["prompt"] = "You are a coder.";

    QJsonObject permissionObj;
    QJsonObject bashPolicy;
    bashPolicy["policy"] = "ask";
    bashPolicy["maxDuration"] = 30;
    permissionObj["bash"] = bashPolicy;
    QJsonObject experimental;
    experimental["flag"] = true;
    permissionObj["experimental"] = experimental;
    coder["permission"] = permissionObj; // nested, unknown to the adapter

    QJsonObject extraNested;
    extraNested["foo"] = 123;
    coder["extra_nested"] = extraNested; // another nested unknown block

    QJsonObject agents;
    agents["coder"] = coder;
    root["agent"] = agents;
    root["default"] = "coder";

    Template tpl = OpencodeSchemaAdapter::loadTemplate(root);
    QVERIFY(tpl.agents.contains("coder"));

    QJsonObject out = OpencodeSchemaAdapter::saveTemplate(tpl);
    const QJsonObject outAgents = out.value("agent").toObject();
    const QJsonObject outCoder = outAgents.value("coder").toObject();

    QVERIFY(outCoder.contains("permission"));
    const QJsonObject outPermission = outCoder.value("permission").toObject();
    QCOMPARE(outPermission.value("bash").toObject().value("policy").toString(), QString("ask"));
    QCOMPARE(outPermission.value("bash").toObject().value("maxDuration").toInt(), 30);
    QCOMPARE(outPermission.value("experimental").toObject().value("flag").toBool(), true);

    QVERIFY(outCoder.contains("extra_nested"));
    QCOMPARE(outCoder.value("extra_nested").toObject().value("foo").toInt(), 123);
}

void TestOpencodeSchemaAdapter::testPermissionObjectPreservedWithoutLegacyFlags()
{
    // When a newer permission object is present, the adapter should not inject
    // legacy scalar permission flags that were not in the original agent JSON.
    QJsonObject root;

    QJsonObject coder;
    coder["mode"] = "primary";

    QJsonObject permissionObj;
    permissionObj["bash"] = QJsonObject{{"policy", "ask"}};
    coder["permission"] = permissionObj;

    QJsonObject agents;
    agents["coder"] = coder;
    root["agent"] = agents;
    root["default"] = "coder";

    Template tpl = OpencodeSchemaAdapter::loadTemplate(root);

    // Flip an internal permission value; this should not cause new
    // top-level permission flags to be created when we save.
    auto it = tpl.agents.find("coder");
    QVERIFY(it != tpl.agents.end());
    it->bash = AgentDef::Permission::Deny;

    QJsonObject out = OpencodeSchemaAdapter::saveTemplate(tpl);
    const QJsonObject outAgents = out.value("agent").toObject();
    const QJsonObject outCoder = outAgents.value("coder").toObject();

    // permission object is still present
    QVERIFY(outCoder.contains("permission"));

    // No legacy scalar permission keys should have been introduced
    QVERIFY(!outCoder.contains("bash"));
    QVERIFY(!outCoder.contains("edit"));
    QVERIFY(!outCoder.contains("task"));
    QVERIFY(!outCoder.contains("read"));
    QVERIFY(!outCoder.contains("grep"));
    QVERIFY(!outCoder.contains("glob"));
}

QTEST_MAIN(TestOpencodeSchemaAdapter)
#include "test_opencode_schema_adapter.moc"
