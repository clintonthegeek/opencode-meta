#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "generation.h"
#include "models/Profile.h"
#include "models/Template.h"

class TestGeneration : public QObject
{
    Q_OBJECT

private slots:
    void schemaPresent();
    void defaultAgentIncluded();
    void modelAssignmentInjected();
    void missingModelAssignmentOmitted();
    void globalOverridesAtTopLevel();
    void emptyTemplateAndProfile();
};

static Template makeTemplate()
{
    Template t;
    t.id = QStringLiteral("tpl");
    t.name = QStringLiteral("Test Template");
    t.defaultAgent = QStringLiteral("main");

    AgentDef main;
    main.mode = AgentDef::Mode::Primary;
    main.description = QStringLiteral("Main agent");
    main.prompt = QJsonValue(QStringLiteral("You help the user."));
    main.bash = AgentDef::Permission::Allow;
    t.agents[QStringLiteral("main")] = main;

    AgentDef sub;
    sub.mode = AgentDef::Mode::Subagent;
    sub.description = QStringLiteral("Sub agent");
    sub.prompt = QJsonValue(QStringLiteral("You support the main agent."));
    t.agents[QStringLiteral("sub")] = sub;

    return t;
}

void TestGeneration::schemaPresent()
{
    const Template t = makeTemplate();
    Profile p;
    p.templateId = t.id;

    const QJsonObject out = renderProfileToConfig(t, p);
    QCOMPARE(out.value(QStringLiteral("$schema")).toString(),
             QStringLiteral("https://opencode.ai/config.json"));
}

void TestGeneration::defaultAgentIncluded()
{
    const Template t = makeTemplate();
    Profile p;
    p.templateId = t.id;

    const QJsonObject out = renderProfileToConfig(t, p);
    QCOMPARE(out.value(QStringLiteral("default_agent")).toString(),
             QStringLiteral("main"));
}

void TestGeneration::modelAssignmentInjected()
{
    Template t = makeTemplate();
    Profile p;
    p.templateId = t.id;
    p.modelAssignments[QStringLiteral("main")] = QStringLiteral("anthropic/claude-sonnet-4-6");

    const QJsonObject out = renderProfileToConfig(t, p);
    const QJsonObject agents = out.value(QStringLiteral("agents")).toObject();
    QVERIFY(agents.contains(QStringLiteral("main")));
    QCOMPARE(agents.value(QStringLiteral("main")).toObject().value(QStringLiteral("model")).toString(),
             QStringLiteral("anthropic/claude-sonnet-4-6"));
}

void TestGeneration::missingModelAssignmentOmitted()
{
    // "sub" has no assignment — "model" key should be absent
    Template t = makeTemplate();
    Profile p;
    p.templateId = t.id;
    p.modelAssignments[QStringLiteral("main")] = QStringLiteral("anthropic/claude-sonnet-4-6");

    const QJsonObject out = renderProfileToConfig(t, p);
    const QJsonObject agents = out.value(QStringLiteral("agents")).toObject();
    const QJsonObject subObj = agents.value(QStringLiteral("sub")).toObject();
    QVERIFY(!subObj.contains(QStringLiteral("model")));
}

void TestGeneration::globalOverridesAtTopLevel()
{
    const Template t = makeTemplate();
    Profile p;
    p.templateId = t.id;
    p.globalOverrides.insert(QStringLiteral("small_model"),
                             QStringLiteral("anthropic/claude-haiku-4-5"));

    const QJsonObject out = renderProfileToConfig(t, p);
    QCOMPARE(out.value(QStringLiteral("small_model")).toString(),
             QStringLiteral("anthropic/claude-haiku-4-5"));
}

void TestGeneration::emptyTemplateAndProfile()
{
    Template t;
    Profile p;
    const QJsonObject out = renderProfileToConfig(t, p);
    QVERIFY(out.contains(QStringLiteral("$schema")));
    QVERIFY(!out.contains(QStringLiteral("default_agent")));
    QVERIFY(out.value(QStringLiteral("agents")).toObject().isEmpty());
}

QTEST_MAIN(TestGeneration)
#include "test_generation.moc"
