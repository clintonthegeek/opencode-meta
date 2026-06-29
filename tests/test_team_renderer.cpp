#include <QTest>
#include <QJsonArray>
#include <QJsonObject>

#include "generation/ContractChecker.h"
#include "generation/TeamRenderer.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"

class TestTeamRenderer : public QObject
{
    Q_OBJECT

private slots:
    void basicRendering();
    void toolsKeyIsAbsentByDefault();
    void toolsKeyIsAbsentEvenWhenRoleHasTools();
    void subagentInjectsTaskAllow();
    void subagentRespectsExplicitTaskDeny();
    void primaryModeDoesNotInjectTaskAllow();
    void dropsTopLevelPermissionsMirror();
    void readOnlyPrimaryOmitsEditAndBash();
    void readOnlyPrimaryPreservesReadAllow();
    void readOnlySubagentKeepsEditAndBash();
    void stripsNonCanonicalPermissionKeys();
    void metadataLiftsOptionalEntries();
    void providerOptionsAreLifted();
};

namespace {

QJsonObject renderSingleAgentTeam(const Role &buildRole)
{
    Specialist spec;
    spec.id = QStringLiteral("spec-") + buildRole.id;
    spec.roleId = buildRole.id;
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.promptOverride = QJsonValue(QStringLiteral("Override prompt"));

    Team team;
    team.id = QStringLiteral("team-1");
    team.name = QStringLiteral("Single agent team");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding teamBinding;
    teamBinding.roleId = buildRole.id;
    teamBinding.specialistId = spec.id;
    team.specialists.append(teamBinding);

    QMap<QString, Specialist> specialists;
    specialists.insert(spec.id, spec);

    QMap<QString, Role> roles;
    roles.insert(buildRole.id, buildRole);

    return TeamRenderer::render(team, specialists, roles);
}

} // namespace

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

    const QJsonObject out = renderSingleAgentTeam(buildRole);

    // Basic structural assertions: schema, default_agent, and agent map.
    QCOMPARE(out.value(QStringLiteral("$schema")).toString(),
             QStringLiteral("https://opencode.ai/config.json"));

    QCOMPARE(out.value(QStringLiteral("default_agent")).toString(),
             QStringLiteral("build"));

    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    QVERIFY(agents.contains(QStringLiteral("build")));

    const QJsonObject buildAgent = agents.value(QStringLiteral("build")).toObject();
    QCOMPARE(buildAgent.value(QStringLiteral("model")).toString(),
             QStringLiteral("anthropic/claude-sonnet-4-6"));
    QCOMPARE(buildAgent.value(QStringLiteral("prompt")).toString(),
             QStringLiteral("Override prompt"));
    QCOMPARE(buildAgent.value(QStringLiteral("mode")).toString(), QStringLiteral("primary"));

    const QJsonObject permissionObj = buildAgent.value(QStringLiteral("permission")).toObject();
    QCOMPARE(permissionObj.value(QStringLiteral("bash")).toString(), QStringLiteral("ask"));
}

void TestTeamRenderer::toolsKeyIsAbsentByDefault()
{
    // Phase C1-1 / D-4: the renderer stops emitting the deprecated `tools`
    // map. Default `Role.permissions`-only setup must NOT carry a `tools`
    // key on the rendered agent.
    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.mode = Role::Mode::Primary;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    buildRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(buildRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject buildAgent = agents.value(QStringLiteral("build")).toObject();

    QVERIFY(!buildAgent.contains(QStringLiteral("tools")));
}

void TestTeamRenderer::toolsKeyIsAbsentEvenWhenRoleHasTools()
{
    // Phase C1-1 / D-4: even if a legacy Role carries `tools` (e.g. loaded
    // from a saved file that still held the deprecated map), the renderer
    // MUST NOT emit it. Today's `Role::tools` always carries deprecated
    // `{name: true|false}` entries — no non-deprecated key can survive, so
    // the absence is unconditional until the opencode spec introduces a
    // non-deprecated `tools` key.
    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.mode = Role::Mode::Primary;

    QJsonObject tools;
    tools.insert(QStringLiteral("bash"), QJsonValue(true));
    tools.insert(QStringLiteral("read"), QJsonValue(false));
    buildRole.tools = tools;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    buildRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(buildRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject buildAgent = agents.value(QStringLiteral("build")).toObject();

    QVERIFY(!buildAgent.contains(QStringLiteral("tools")));
    // `permission` still surfaces — the modern key wins.
    QVERIFY(buildAgent.contains(QStringLiteral("permission")));
}

void TestTeamRenderer::subagentInjectsTaskAllow()
{
    // Phase C1-2 / D-3: per OPENCODE-CONFIG-INTROSPECTION §6.4 the
    // runtime force-denies any subagent without an explicit `task:
    // allow`. Renderer must inject it when mode is Subagent and the
    // Role has no explicit `task` rule.
    Role subRole;
    subRole.id = QStringLiteral("explore");
    subRole.mode = Role::Mode::Subagent;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("read"), QStringLiteral("allow"));
    subRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(subRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject exploreAgent = agents.value(QStringLiteral("explore")).toObject();

    const QJsonObject permissionObj = exploreAgent.value(QStringLiteral("permission")).toObject();
    QCOMPARE(permissionObj.value(QStringLiteral("task")).toString(),
             QStringLiteral("allow"));
    // Existing user rules survive the injection.
    QCOMPARE(permissionObj.value(QStringLiteral("bash")).toString(),
             QStringLiteral("ask"));
    QCOMPARE(permissionObj.value(QStringLiteral("read")).toString(),
             QStringLiteral("allow"));

    // v2 mirror also carries the rule.
    const QJsonArray v2Permissions =
        exploreAgent.value(QStringLiteral("permissions")).toArray();
    bool taskRuleFound = false;
    for (const QJsonValue &v : v2Permissions) {
        const QJsonObject rule = v.toObject();
        if (rule.value(QStringLiteral("action")).toString() == QLatin1String("task")) {
            taskRuleFound = true;
            QCOMPARE(rule.value(QStringLiteral("effect")).toString(),
                     QStringLiteral("allow"));
        }
    }
    QVERIFY(taskRuleFound);
}

void TestTeamRenderer::subagentRespectsExplicitTaskDeny()
{
    // Phase C1-2 / D-3: user-set `task: "deny"` MUST be preserved
    // verbatim — explicit rules are the whole point of having the
    // editor surface the row in the first place.
    Role subRole;
    subRole.id = QStringLiteral("explore");
    subRole.mode = Role::Mode::Subagent;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("task"), QStringLiteral("deny"));
    subRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(subRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject exploreAgent = agents.value(QStringLiteral("explore")).toObject();

    const QJsonObject permissionObj = exploreAgent.value(QStringLiteral("permission")).toObject();
    QCOMPARE(permissionObj.value(QStringLiteral("task")).toString(),
             QStringLiteral("deny"));

    // v2 mirror also picks up the explicit deny.
    const QJsonArray v2Permissions =
        exploreAgent.value(QStringLiteral("permissions")).toArray();
    bool taskDenyRuleFound = false;
    for (const QJsonValue &v : v2Permissions) {
        const QJsonObject rule = v.toObject();
        if (rule.value(QStringLiteral("action")).toString() == QLatin1String("task")
            && rule.value(QStringLiteral("effect")).toString() == QLatin1String("deny")) {
            taskDenyRuleFound = true;
        }
    }
    QVERIFY(taskDenyRuleFound);
}

void TestTeamRenderer::primaryModeDoesNotInjectTaskAllow()
{
    // Phase C1-2 / D-3: only Subagent / All modes get the injection.
    // Primary-mode agents are not delegated to and therefore don't
    // need the escape hatch.
    Role primaryRole;
    primaryRole.id = QStringLiteral("build");
    primaryRole.mode = Role::Mode::Primary;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    primaryRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(primaryRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject buildAgent = agents.value(QStringLiteral("build")).toObject();

    const QJsonObject permissionObj = buildAgent.value(QStringLiteral("permission")).toObject();
    QVERIFY(!permissionObj.contains(QStringLiteral("task")));
}

void TestTeamRenderer::dropsTopLevelPermissionsMirror()
{
    // Phase C1-3 / D-1: the renderer must NOT auto-mirror top-level
    // `permission -> permissions` or `attachment -> attachments`. Per
    // OPENCODE-CONFIG-INTROSPECTION §6.2 / packages/schema/src/permission.ts:64
    // the v2 form of those keys is structurally different from v1
    // (top-level `permissions` is a `Permission.Ruleset` array, and
    // `attachments` is a list-shaped attachment surface), so a 1:1
    // object mirror is wrong. The renderer's old behavior auto-added
    // such mirrors whenever the v1 source key was present; C1-3
    // removes those arms to keep the file from emitting invalid v2
    // sidecars that the migration bridge at
    // packages/core/src/v1/config/migrate.ts:35 would silently coerce
    // (or `opencode debug config` would reject as InvalidError).
    //
    // Today Team/Role do not produce top-level `permission` or
    // `attachment`, so we verify three properties:
    //   (a) the rendered config never carries a top-level `permissions`
    //       OR `attachments` key (the dead-code arms that used to add
    //       them are inert);
    //   (b) a test fixture that DOES carry a top-level `permission`
    //       (simulating a future Team field) is still accepted by
    //       `ContractChecker::validate` — top-level `permission` is a
    //       legal v1 key per report §4;
    //   (c) the same fixture is NOT accidentally upgraded into a
    //       Ruleset-shaped `permissions` mirror inside the renderer
    //       path (the renderer's contract is "v1 emits v1; v2 mirrors
    //       are emitted only when the v2 form is structurally correct
    //       AND a top-level source is present").
    Role primaryRole;
    primaryRole.id = QStringLiteral("build");
    primaryRole.mode = Role::Mode::Primary;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    primaryRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(primaryRole);

    // (a) — renderer's own output
    QVERIFY2(!out.contains(QStringLiteral("permissions")),
             "rendered config MUST NOT contain a top-level `permissions` key "
             "(C1-3 dropped the dead-code `permission -> permissions` mirror)");
    QVERIFY2(!out.contains(QStringLiteral("attachments")),
             "rendered config MUST NOT contain a top-level `attachments` key "
             "(C1-3 dropped the dead-code `attachment -> attachments` mirror)");

    // (b) — fixture with top-level permission still validates.
    QJsonObject fixture = out;
    QJsonObject topPerm;
    topPerm.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    fixture.insert(QStringLiteral("permission"), topPerm);

    QString err;
    QVERIFY2(ContractChecker::validate(fixture, &err),
             qPrintable(QStringLiteral(
                 "top-level `permission` is allowed by report §4; validator "
                 "should accept. error: %1").arg(err)));

    // (c) — the fixture MUST NOT have grown a top-level `permissions`
    // mirror (we never invoked the renderer against the fixture; but
    // since no rendering step could have produced one, the assertion
    // is a belt-and-braces guard against future regressions that
    // re-introduce a hidden 1:1 mirror in some other code path).
    QVERIFY2(!fixture.contains(QStringLiteral("permissions")),
             "fixture with top-level `permission` MUST NOT carry a top-level "
             "`permissions` mirror either");
}

void TestTeamRenderer::readOnlyPrimaryOmitsEditAndBash()
{
    // Phase C3-2 / D-5: when role.mode == Primary AND role.readOnly
    // == true, the renderer must omit `edit` and `bash` from the
    // emitted `permission` object regardless of what the Role stored.
    // This is report §6.4 second-class fix — the read-only primary
    // explorer pattern that weak-model tool-calling could otherwise
    // crash on.
    Role buildRole;
    buildRole.id = QStringLiteral("readonly-build");
    buildRole.mode = Role::Mode::Primary;
    buildRole.readOnly = true;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("edit"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("read"), QStringLiteral("allow"));
    buildRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(buildRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject buildAgent = agents.value(QStringLiteral("readonly-build")).toObject();

    const QJsonObject permissionObj =
        buildAgent.value(QStringLiteral("permission")).toObject();

    QVERIFY2(!permissionObj.contains(QStringLiteral("edit")),
             "read-only primary role MUST NOT emit 'edit' permission key "
             "after C3-2; the opencode defaults take over");
    QVERIFY2(!permissionObj.contains(QStringLiteral("bash")),
             "read-only primary role MUST NOT emit 'bash' permission key "
             "after C3-2; the opencode defaults take over");
}

void TestTeamRenderer::readOnlyPrimaryPreservesReadAllow()
{
    // Phase C3-2 defensive guard: read-only + primary keeps the
    // non-write keys (read, glob, grep, ...) intact. Only the two
    // dangerous tool-call surfaces (edit / bash) are dropped.
    Role buildRole;
    buildRole.id = QStringLiteral("readonly-build");
    buildRole.mode = Role::Mode::Primary;
    buildRole.readOnly = true;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("edit"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("read"), QStringLiteral("allow"));
    permissions.insert(QStringLiteral("grep"), QStringLiteral("allow"));
    buildRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(buildRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject buildAgent = agents.value(QStringLiteral("readonly-build")).toObject();
    const QJsonObject permissionObj =
        buildAgent.value(QStringLiteral("permission")).toObject();

    QCOMPARE(permissionObj.value(QStringLiteral("read")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(permissionObj.value(QStringLiteral("grep")).toString(),
             QStringLiteral("allow"));
    QVERIFY(!permissionObj.contains(QStringLiteral("edit")));
    QVERIFY(!permissionObj.contains(QStringLiteral("bash")));
}

void TestTeamRenderer::readOnlySubagentKeepsEditAndBash()
{
    // Phase C3-2 / D-5 boundary case: subagent + readOnly. Per the
    // ROADMAP D-5 row, the omission is mode-gated (only `Primary`),
    // so a Subagent role with readOnly=true still keeps its edit/
    // bash entries verbatim. Subagents may legitimately need narrow
    // write access even when their STRUCTURAL description is
    // read-only.
    Role subRole;
    subRole.id = QStringLiteral("readonly-explore");
    subRole.mode = Role::Mode::Subagent;
    subRole.readOnly = true;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("edit"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("read"), QStringLiteral("allow"));
    subRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(subRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject buildAgent = agents.value(QStringLiteral("readonly-explore")).toObject();
    const QJsonObject permissionObj =
        buildAgent.value(QStringLiteral("permission")).toObject();

    QCOMPARE(permissionObj.value(QStringLiteral("edit")).toString(),
             QStringLiteral("ask"));
    QCOMPARE(permissionObj.value(QStringLiteral("bash")).toString(),
             QStringLiteral("ask"));
    QCOMPARE(permissionObj.value(QStringLiteral("read")).toString(),
             QStringLiteral("allow"));
}

void TestTeamRenderer::stripsNonCanonicalPermissionKeys()
{
    // Phase C4-1 / D-6: the renderer strips non-canonical permission
    // keys (anything not in the 15-key §6.1 set) before emission. The
    // file MUST only ever carry canonical keys; the qWarning is emitted
    // so the user can fix the source Role. The edit path is permissive —
    // only the emission is strict.
    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.mode = Role::Mode::Primary;

    QJsonObject permissions;
    permissions.insert(QStringLiteral("read"), QStringLiteral("allow"));
    permissions.insert(QStringLiteral("edit"), QStringLiteral("ask"));
    permissions.insert(QStringLiteral("writefile"), QStringLiteral("ask")); // non-canonical
    permissions.insert(QStringLiteral("mcp_custom"), QStringLiteral("deny")); // non-canonical
    buildRole.permissions = permissions;

    const QJsonObject out = renderSingleAgentTeam(buildRole);
    const QJsonObject agents = out.value(QStringLiteral("agent")).toObject();
    const QJsonObject buildAgent = agents.value(QStringLiteral("build")).toObject();
    const QJsonObject permissionObj =
        buildAgent.value(QStringLiteral("permission")).toObject();

    QCOMPARE(permissionObj.value(QStringLiteral("read")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(permissionObj.value(QStringLiteral("edit")).toString(),
             QStringLiteral("ask"));
    QVERIFY2(!permissionObj.contains(QStringLiteral("writefile")),
             "non-canonical permission key 'writefile' MUST be stripped "
             "by the renderer per C4-1 / D-6");
    QVERIFY2(!permissionObj.contains(QStringLiteral("mcp_custom")),
             "non-canonical permission key 'mcp_custom' MUST be stripped "
             "by the renderer per C4-1 / D-6");
}

void TestTeamRenderer::metadataLiftsOptionalEntries()
{
    // Phase C6-1: Role::metadata.mcpEntries / lspEntries /
    // formatterEntries / referenceEntries each lift to the
    // corresponding top-level key in the rendered `opencode.json`.
    // The source contract: `Role::metadata` is a free-form JSON
    // object whose documented sub-key is `<sub>Entries` and whose
    // value is itself a JSON object whose keys/values copy through.
    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.mode = Role::Mode::Primary;

    QJsonObject mcpEntries;
    QJsonObject mcpValue;
    mcpValue.insert(QStringLiteral("url"), QStringLiteral("stdio://my-server"));
    mcpValue.insert(QStringLiteral("enabled"), QJsonValue(true));
    mcpEntries.insert(QStringLiteral("myMcp"), mcpValue);

    QJsonObject lspEntries;
    QJsonObject lspValue;
    lspValue.insert(QStringLiteral("command"), QStringLiteral("mylsp"));
    lspEntries.insert(QStringLiteral("myLsp"), lspValue);

    QJsonObject formatterEntries;
    QJsonObject formatterValue;
    formatterValue.insert(QStringLiteral("command"), QStringLiteral("myfmt"));
    formatterEntries.insert(QStringLiteral("myFmt"), formatterValue);

    QJsonObject referenceEntries;
    QJsonObject refValue;
    refValue.insert(QStringLiteral("path"), QStringLiteral("./docs/spec.md"));
    referenceEntries.insert(QStringLiteral("localSpec"), refValue);

    QJsonObject md;
    md.insert(QStringLiteral("mcpEntries"), mcpEntries);
    md.insert(QStringLiteral("lspEntries"), lspEntries);
    md.insert(QStringLiteral("formatterEntries"), formatterEntries);
    md.insert(QStringLiteral("referenceEntries"), referenceEntries);
    buildRole.metadata = md;

    const QJsonObject out = renderSingleAgentTeam(buildRole);

    // Each sub-key lifts to its top-level partner.
    const QJsonObject mcp = out.value(QStringLiteral("mcp")).toObject();
    QCOMPARE(mcp.value(QStringLiteral("myMcp"))
                .toObject()
                .value(QStringLiteral("url"))
                .toString(),
             QStringLiteral("stdio://my-server"));
    QCOMPARE(mcp.value(QStringLiteral("myMcp"))
                .toObject()
                .value(QStringLiteral("enabled"))
                .toBool(),
             true);

    const QJsonObject lsp = out.value(QStringLiteral("lsp")).toObject();
    QCOMPARE(lsp.value(QStringLiteral("myLsp"))
                .toObject()
                .value(QStringLiteral("command"))
                .toString(),
             QStringLiteral("mylsp"));

    const QJsonObject formatter =
        out.value(QStringLiteral("formatter")).toObject();
    QCOMPARE(formatter.value(QStringLiteral("myFmt"))
                .toObject()
                .value(QStringLiteral("command"))
                .toString(),
             QStringLiteral("myfmt"));

    const QJsonObject references =
        out.value(QStringLiteral("references")).toObject();
    QCOMPARE(references.value(QStringLiteral("localSpec"))
                .toObject()
                .value(QStringLiteral("path"))
                .toString(),
             QStringLiteral("./docs/spec.md"));

    // Invalid shape (non-object) MUST be skipped, not crash.
    Role badShapeRole;
    badShapeRole.id = QStringLiteral("badshape");
    badShapeRole.mode = Role::Mode::Primary;
    QJsonObject badMd;
    badMd.insert(QStringLiteral("mcpEntries"), QJsonValue(QStringLiteral("not-an-object")));
    badShapeRole.metadata = badMd;

    // We don't strictly require the bad-shape role to render without
    // crash (it gracefully skips with a qWarning). Just verify the
    // renderer doesn't throw / segfault.
    const QJsonObject _unused = renderSingleAgentTeam(buildRole);
    Q_UNUSED(_unused);
}

void TestTeamRenderer::providerOptionsAreLifted()
{
    // Phase C6-3: Role::metadata.providerOptions lifts to top-level
    // `provider` AND the v2 mirror `providers` (same shape per D-1).
    // The schema is `Record<id, ConfigProviderV1.Info>` so each entry
    // wraps the provider details inside an id-keyed object.
    Role roleA;
    roleA.id = QStringLiteral("a");
    roleA.mode = Role::Mode::Primary;
    QJsonObject aMd;
    QJsonObject aProviders; // { "<id>": ConfigProviderV1.Info }
    QJsonObject exampleProvider;
    exampleProvider.insert(QStringLiteral("baseURL"),
                           QStringLiteral("https://api.example.com"));
    exampleProvider.insert(QStringLiteral("apiKey"),
                           QJsonValue(QStringLiteral("ENV:EXAMPLE")));
    aProviders.insert(QStringLiteral("example"), exampleProvider);
    aMd.insert(QStringLiteral("providerOptions"), aProviders);
    roleA.metadata = aMd;

    const QJsonObject out = renderSingleAgentTeam(roleA);

    // Top-level `provider` lift:
    const QJsonObject providerA =
        out.value(QStringLiteral("provider")).toObject();
    QVERIFY(providerA.contains(QStringLiteral("example")));
    QCOMPARE(providerA.value(QStringLiteral("example"))
                .toObject()
                .value(QStringLiteral("baseURL"))
                .toString(),
             QStringLiteral("https://api.example.com"));
    QCOMPARE(providerA.value(QStringLiteral("example"))
                .toObject()
                .value(QStringLiteral("apiKey"))
                .toString(),
             QStringLiteral("ENV:EXAMPLE"));

    // v2 `providers` mirror (per D-1 / same-shape):
    const QJsonObject providersMirror =
        out.value(QStringLiteral("providers")).toObject();
    QVERIFY(providersMirror.contains(QStringLiteral("example")));
    QCOMPARE(providersMirror.value(QStringLiteral("example"))
                .toObject()
                .value(QStringLiteral("baseURL"))
                .toString(),
             QStringLiteral("https://api.example.com"));
}

QTEST_MAIN(TestTeamRenderer)
#include "test_team_renderer.moc"
