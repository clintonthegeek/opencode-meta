#include <QTest>
#include <QJsonObject>

#include "generation/TeamRenderer.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"

class TestTeamRenderer : public QObject
{
    Q_OBJECT

private slots:
    void basicRendering();
};

void TestTeamRenderer::basicRendering()
{
    // Minimal smoke test that verifies TeamRenderer links and produces a
    // structurally valid config object.

    // Define a Role (build) with a simple system prompt.
    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.description = QStringLiteral("Primary build agent");
    buildRole.systemPrompt = QJsonValue(QStringLiteral("You are the primary build agent."));
    buildRole.mode = Role::Mode::Primary;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    buildRole.permissions = permissions;

    // One Specialist filling the build role.
    Specialist spec;
    spec.id = QStringLiteral("spec-build-1");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.promptOverride = QJsonValue(QStringLiteral("Override prompt"));

    // Team wiring the Role to the Specialist and marking it primary.
    Team team;
    team.id = QStringLiteral("team-1");
    team.name = QStringLiteral("Single agent team");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding teamBinding;
    teamBinding.roleId = QStringLiteral("build");
    teamBinding.specialistId = spec.id;
    team.specialists.append(teamBinding);

    QMap<QString, Specialist> specialists;
    specialists.insert(spec.id, spec);

    QMap<QString, Role> roles;
    roles.insert(buildRole.id, buildRole);

    const QJsonObject out = TeamRenderer::render(team, specialists, roles);

    // Basic structural assertions: schema, default_agent, and agent map.
    QCOMPARE(out.value(QStringLiteral("$schema")).toString(),
             QStringLiteral("https://opencode.ai/config.json"));

    QCOMPARE(out.value(QStringLiteral("default_agent")).toString(),
             QStringLiteral("build"));

    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    QVERIFY(agents.contains(QStringLiteral("build")));

    const QJsonObject buildAgent = agents.value(QStringLiteral("build")).toObject();
    QCOMPARE(buildAgent.value(QStringLiteral("model")).toString(), spec.modelId);
    QCOMPARE(buildAgent.value(QStringLiteral("prompt")).toString(),
             QStringLiteral("Override prompt"));
    QCOMPARE(buildAgent.value(QStringLiteral("mode")).toString(), QStringLiteral("primary"));

    const QJsonObject permissionObj = buildAgent.value(QStringLiteral("permission")).toObject();
    QCOMPARE(permissionObj.value(QStringLiteral("bash")).toString(), QStringLiteral("ask"));
}

QTEST_MAIN(TestTeamRenderer)
#include "test_team_renderer.moc"
