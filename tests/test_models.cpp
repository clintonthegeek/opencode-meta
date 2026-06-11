#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>

#include "models/Template.h"
#include "models/Profile.h"

class TestModels : public QObject
{
    Q_OBJECT

private slots:
    void agentDef_roundTrip();
    void template_roundTrip();
    void template_emptyRoundTrip();
    void profile_roundTrip();
    void profile_emptyRoundTrip();
    void agentDef_permissions_roundTrip();
};

void TestModels::agentDef_roundTrip()
{
    AgentDef def;
    def.mode = AgentDef::Mode::Primary;
    def.description = QStringLiteral("A primary agent");
    def.prompt = QJsonValue(QStringLiteral("Do something useful."));
    def.edit = AgentDef::Permission::Allow;
    def.bash = AgentDef::Permission::Deny;
    def.task = AgentDef::Permission::Ask;
    def.read = AgentDef::Permission::Allow;
    def.grep = AgentDef::Permission::Allow;
    def.glob = AgentDef::Permission::Ask;

    const QJsonObject json = def.toJson();
    const AgentDef restored = AgentDef::fromJson(json);

    QCOMPARE(restored.mode, AgentDef::Mode::Primary);
    QCOMPARE(restored.description, def.description);
    QCOMPARE(restored.prompt, def.prompt);
    QCOMPARE(restored.edit, AgentDef::Permission::Allow);
    QCOMPARE(restored.bash, AgentDef::Permission::Deny);
    QCOMPARE(restored.task, AgentDef::Permission::Ask);
    QCOMPARE(restored.read, AgentDef::Permission::Allow);
    QCOMPARE(restored.grep, AgentDef::Permission::Allow);
    QCOMPARE(restored.glob, AgentDef::Permission::Ask);
}

void TestModels::agentDef_permissions_roundTrip()
{
    // Verify all three permission values survive a round-trip
    const QStringList permStrings = {QStringLiteral("allow"), QStringLiteral("deny"), QStringLiteral("ask")};
    for (const QString &s : permStrings) {
        const AgentDef::Permission p = AgentDef::permissionFromString(s);
        QCOMPARE(AgentDef::permissionToString(p), s);
    }

    // Verify both mode values survive a round-trip
    QCOMPARE(AgentDef::modeToString(AgentDef::Mode::Primary), QStringLiteral("primary"));
    QCOMPARE(AgentDef::modeToString(AgentDef::Mode::Subagent), QStringLiteral("subagent"));
    QCOMPARE(AgentDef::modeFromString(QStringLiteral("primary")), AgentDef::Mode::Primary);
    QCOMPARE(AgentDef::modeFromString(QStringLiteral("subagent")), AgentDef::Mode::Subagent);
}

void TestModels::template_roundTrip()
{
    Template t;
    t.id = QStringLiteral("my-template");
    t.name = QStringLiteral("My Template");
    t.version = QStringLiteral("1.0.0");
    t.defaultAgent = QStringLiteral("main");
    t.metadata[QStringLiteral("created")] = QStringLiteral("2025-01-01T00:00:00Z");

    AgentDef main;
    main.mode = AgentDef::Mode::Primary;
    main.description = QStringLiteral("Main agent");
    main.prompt = QJsonValue(QStringLiteral("You are helpful."));
    main.bash = AgentDef::Permission::Allow;
    t.agents[QStringLiteral("main")] = main;

    AgentDef sub;
    sub.mode = AgentDef::Mode::Subagent;
    sub.description = QStringLiteral("Sub agent");
    sub.prompt = QJsonValue(QJsonObject{{QStringLiteral("file"), QStringLiteral("./prompts/sub.md")}});
    t.agents[QStringLiteral("sub")] = sub;

    const QJsonObject json = t.toJson();
    const Template restored = Template::fromJson(json);

    QCOMPARE(restored.id, t.id);
    QCOMPARE(restored.name, t.name);
    QCOMPARE(restored.version, t.version);
    QCOMPARE(restored.defaultAgent, t.defaultAgent);
    QCOMPARE(restored.metadata[QStringLiteral("created")], t.metadata[QStringLiteral("created")]);
    QCOMPARE(restored.agents.size(), 2);

    const AgentDef &restoredMain = restored.agents.value(QStringLiteral("main"));
    QCOMPARE(restoredMain.mode, AgentDef::Mode::Primary);
    QCOMPARE(restoredMain.description, main.description);
    QCOMPARE(restoredMain.bash, AgentDef::Permission::Allow);

    const AgentDef &restoredSub = restored.agents.value(QStringLiteral("sub"));
    QCOMPARE(restoredSub.mode, AgentDef::Mode::Subagent);
    QVERIFY(restoredSub.prompt.isObject());
    QCOMPARE(restoredSub.prompt.toObject().value(QStringLiteral("file")).toString(),
             QStringLiteral("./prompts/sub.md"));
}

void TestModels::template_emptyRoundTrip()
{
    Template t;
    const QJsonObject json = t.toJson();
    const Template restored = Template::fromJson(json);
    QVERIFY(restored.id.isEmpty());
    QVERIFY(restored.agents.isEmpty());
}

void TestModels::profile_roundTrip()
{
    Profile p;
    p.id = QStringLiteral("my-profile");
    p.name = QStringLiteral("My Profile");
    p.templateId = QStringLiteral("my-template");
    p.modelAssignments[QStringLiteral("main")] = QStringLiteral("anthropic/claude-sonnet-4-6");
    p.modelAssignments[QStringLiteral("sub")] = QStringLiteral("openai/gpt-4o");
    p.globalOverrides.insert(QStringLiteral("small_model"), QStringLiteral("anthropic/claude-haiku-4-5"));
    p.metadata[QStringLiteral("created")] = QStringLiteral("2025-01-01T00:00:00Z");

    const QJsonObject json = p.toJson();
    const Profile restored = Profile::fromJson(json);

    QCOMPARE(restored.id, p.id);
    QCOMPARE(restored.name, p.name);
    QCOMPARE(restored.templateId, p.templateId);
    QCOMPARE(restored.modelAssignments.size(), 2);
    QCOMPARE(restored.modelAssignments.value(QStringLiteral("main")),
             QStringLiteral("anthropic/claude-sonnet-4-6"));
    QCOMPARE(restored.modelAssignments.value(QStringLiteral("sub")),
             QStringLiteral("openai/gpt-4o"));
    QCOMPARE(restored.globalOverrides.value(QStringLiteral("small_model")).toString(),
             QStringLiteral("anthropic/claude-haiku-4-5"));
    QCOMPARE(restored.metadata[QStringLiteral("created")], p.metadata[QStringLiteral("created")]);
}

void TestModels::profile_emptyRoundTrip()
{
    Profile p;
    const QJsonObject json = p.toJson();
    const Profile restored = Profile::fromJson(json);
    QVERIFY(restored.id.isEmpty());
    QVERIFY(restored.modelAssignments.isEmpty());
}

QTEST_MAIN(TestModels)
#include "test_models.moc"
