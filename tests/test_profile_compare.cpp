// test_profile_compare.cpp
// Headless tests for profile config comparison helpers (M3).

#include <QTest>
#include <QJsonObject>

#include "generation.h"
#include "models/Profile.h"
#include "models/Template.h"

static Template makeTemplateForCompare()
{
    Template t;
    t.id = QStringLiteral("tpl-compare");
    t.name = QStringLiteral("Compare Template");
    t.defaultAgent = QStringLiteral("main");

    AgentDef main;
    main.mode = AgentDef::Mode::Primary;
    main.description = QStringLiteral("Main agent");
    main.prompt = QJsonValue(QStringLiteral("You help the user."));
    t.agents[QStringLiteral("main")] = main;

    return t;
}

class TestProfileCompare : public QObject
{
    Q_OBJECT

private slots:
    void diff_detectsTopLevelModelChange();
    void diff_reportsNoDifferencesWhenConfigsMatch();
};

void TestProfileCompare::diff_detectsTopLevelModelChange()
{
    const Template t = makeTemplateForCompare();

    Profile p1;
    p1.templateId = t.id;
    p1.modelAssignments.insert(QStringLiteral("main"), QStringLiteral("provider/model-a"));

    Profile p2 = p1;
    p2.modelAssignments[QStringLiteral("main")] = QStringLiteral("provider/model-b");

    const QJsonObject cfg1 = renderProfileToConfig(t, p1);
    const QJsonObject cfg2 = renderProfileToConfig(t, p2);

    const QStringList summary = summarizeTopLevelConfigDiff(cfg1, cfg2);

    // We expect at least one difference mentioning the agents key.
    bool sawAgentsDiff = false;
    for (const QString &line : summary) {
        if (line.contains(QStringLiteral("agents")) && line.contains(QStringLiteral("differs"))) {
            sawAgentsDiff = true;
            break;
        }
    }
    QVERIFY2(sawAgentsDiff, "Expected agents-related difference in summary");
}

void TestProfileCompare::diff_reportsNoDifferencesWhenConfigsMatch()
{
    const Template t = makeTemplateForCompare();

    Profile p1;
    p1.templateId = t.id;
    p1.modelAssignments.insert(QStringLiteral("main"), QStringLiteral("provider/model-a"));
    p1.globalOverrides.insert(QStringLiteral("small_model"), QStringLiteral("provider/model-small"));

    Profile p2 = p1;

    const QJsonObject cfg1 = renderProfileToConfig(t, p1);
    const QJsonObject cfg2 = renderProfileToConfig(t, p2);

    const QStringList summary = summarizeTopLevelConfigDiff(cfg1, cfg2);

    // For identical configs we currently collapse to a single informational line.
    QCOMPARE(summary.size(), 1);
    QCOMPARE(summary.first(), QStringLiteral("No top-level differences."));
}

QTEST_MAIN(TestProfileCompare)
#include "test_profile_compare.moc"
