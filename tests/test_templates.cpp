// test_templates.cpp
// Basic tests for Template JSON round-trip and validation.

#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>

#include "models/Template.h"
#include "adapter/OpencodeSchemaAdapter.h"

class TestTemplates : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip_preservesCoreFields();
    void validate_templateFromJson();
};

void TestTemplates::roundTrip_preservesCoreFields()
{
    Template t;
    t.id = QStringLiteral("tpl-1");
    t.name = QStringLiteral("RoundTrip Template");
    t.version = QStringLiteral("1.0.0");
    t.defaultAgent = QStringLiteral("main");

    AgentDef def;
    def.mode = AgentDef::Mode::Primary;
    def.description = QStringLiteral("Main agent");
    def.prompt = QJsonValue(QStringLiteral("You are helpful."));
    def.edit = AgentDef::Permission::Allow;
    def.bash = AgentDef::Permission::Ask;
    def.task = AgentDef::Permission::Deny;
    def.read = AgentDef::Permission::Allow;
    def.grep = AgentDef::Permission::Ask;
    def.glob = AgentDef::Permission::Allow;
    def.tools = QJsonObject{{QStringLiteral("tool-x"), QJsonObject{{QStringLiteral("policy"), QStringLiteral("allow")}}}};

    t.agents.insert(QStringLiteral("main"), def);

    const QJsonObject json = t.toJson();
    const Template t2 = Template::fromJson(json);

    QCOMPARE(t2.id, QStringLiteral("tpl-1"));
    QCOMPARE(t2.name, QStringLiteral("RoundTrip Template"));
    QCOMPARE(t2.version, QStringLiteral("1.0.0"));
    QCOMPARE(t2.defaultAgent, QStringLiteral("main"));
    QVERIFY(t2.agents.contains(QStringLiteral("main")));

    const AgentDef def2 = t2.agents.value(QStringLiteral("main"));
    QCOMPARE(def2.mode, AgentDef::Mode::Primary);
    QCOMPARE(def2.description, QStringLiteral("Main agent"));
    QCOMPARE(def2.prompt.toString(), QStringLiteral("You are helpful."));
    QCOMPARE(def2.edit, AgentDef::Permission::Allow);
    QCOMPARE(def2.bash, AgentDef::Permission::Ask);
    QCOMPARE(def2.task, AgentDef::Permission::Deny);
    QCOMPARE(def2.read, AgentDef::Permission::Allow);
    QCOMPARE(def2.grep, AgentDef::Permission::Ask);
    QCOMPARE(def2.glob, AgentDef::Permission::Allow);
    QVERIFY(def2.tools.contains(QStringLiteral("tool-x")));
}

void TestTemplates::validate_templateFromJson()
{
    // Template::fromJson should produce a structure that passes basic adapter validation.
    QJsonObject json;
    json.insert(QStringLiteral("id"), QStringLiteral("tpl-validate"));
    json.insert(QStringLiteral("name"), QStringLiteral("Validate Template"));
    json.insert(QStringLiteral("default_agent"), QStringLiteral("main"));

    QJsonObject agents;
    QJsonObject main;
    main.insert(QStringLiteral("mode"), QStringLiteral("primary"));
    main.insert(QStringLiteral("description"), QStringLiteral("Main agent"));
    main.insert(QStringLiteral("prompt"), QStringLiteral("You help the user."));
    agents.insert(QStringLiteral("main"), main);
    json.insert(QStringLiteral("agents"), agents);

    const Template t = Template::fromJson(json);
    const auto error = OpencodeSchemaAdapter::validate(t);
    QVERIFY(!error.has_value());
}

QTEST_MAIN(TestTemplates)
#include "test_templates.moc"
