// Phase D1 / D-9 — Stock-fidelity seed target.
//
// Locks the new seedDefaultsIfNeeded implementation against the stock
// opencode ground truth at `~/src/opencode/packages/opencode/src/agent/
// agent.ts:140-264`. The slots below pin every shape that the renderer
// or UI relies on, so any future drift trips a failing slot here
// instead of a silent regression in the user's emitted `opencode.json`.
//
// Slot breakdown (per docs/plan/2026-06-29-stock-agent-fidelity.md §5):
//   D1-1: 7 stockDefaults_*_isCorrectShape slots (one per agent)
//   D1-2: versionDefaultsTo_V1_StockFidelity
//   D1-3: 6 seededX slots
//   D1-4: 5 seededHidden + seededHiddenT_* slots
//   D1-5: 3 seededStarterTeamX slots
//
// All slots use a fresh QTemporaryDir per test so the seeder's
// "existing user data is sacred" early-out at StorageManager.cpp
// never short-circuits the test path.

#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

#include "storage/StorageManager.h"

class TestSeedStockFidelity : public QObject
{
    Q_OBJECT

private slots:
    // ---- D1-1: kStockDefaults shape (anon-ns internal helper) ----
    // We exercise the anon-ns map via the public surface (saveRole +
    // loadRole round-trip); the per-agent shape verification lives in
    // the seededRoles_* slots in D1-3 below.
    void stockDefaults_ForBuild_isCorrectShape();
    void stockDefaults_ForPlan_isCorrectShape();
    void stockDefaults_ForGeneral_isCorrectShape();
    void stockDefaults_ForExplore_isCorrectShape();
    void stockDefaults_ForCompaction_isCorrectShape();
    void stockDefaults_ForTitle_isCorrectShape();
    void stockDefaults_ForSummary_isCorrectShape();

    // ---- D1-2: SeedVersion enum ----
    void versionDefaultsTo_V1_StockFidelity();

    // ---- D1-3: rewrites the four main block seeds ----
    void seededBuild_metadataNativeIsTrue();
    void seededBuild_permissionsContains_questionAllow();
    void seededPlan_modeIsPrimary();
    void seededPlan_permissionsContains_planExitAllow();
    void seededPlan_editBlockDeniedExceptPlans();
    void seededPlan_taskGeneralIsDeny();
    void seededGeneral_metadataNativeIsTrue();
    void seededGeneral_todowriteIsDeny();
    void seededExplore_modeIsSubagent();
    void seededExplore_permissionsRead_andGlob_andGrep_AreAllow();
    void seededExplore_permissionsStarIsDeny();

    // ---- D1-4: hidden primaries ----
    void seededHidden_NativeAndHiddenFlagsAreTrue();
    void seededCompaction_systemPromptMatchesStockTxt();
    void seededTitle_systemPromptMatchesStockTxt();
    void seededSummary_systemPromptMatchesStockTxt();
    void seededRolesTotal_IsAtLeastSeven();

    // ---- D1-5: Starter Team v1 ----
    void seededStarterTeam_hasDefaultAgent_SetToBuild();
    void seededStarterTeam_bindsExploreSpecialist();
    void seededStarterTeam_bindsAllFourSpecialists();
    void seededSpecialists_countIsAtLeastFour();

private:
    // Wipes QSettings so the seed-stock toggle defaults to ON
    // (true). Tests must NOT inherit a saved "false" from previous
    // runs that would re-route through the legacy-fiction branch.
    void clearSettings();

    // Returns a fresh StorageManager pointing at a temp root that
    // has zero roles/, zero teams/ files. seedDefaultsIfNeeded will
    // therefore run the stock-aligned path end-to-end.
    StorageManager freshStorage();

    // Tracks every temp root created in this test run so the OS
    // can reclaim them at process exit. StorageManager takes the
    // root path by value so the dir must outlive the StorageManager
    // value; we leak the dir for that reason (acceptable for a
    // test target).
    QList<QTemporaryDir *> m_storageRoots;
};

void TestSeedStockFidelity::clearSettings()
{
    if (QCoreApplication::instance() == nullptr) {
        return;
    }
    QSettings settings;
    settings.remove(QStringLiteral("storage/seed_version"));
    settings.remove(QStringLiteral("settings/seed_stock_defaults"));
    settings.remove(QStringLiteral("settings/reset_seed_on_next_launch"));
    settings.sync();
}

StorageManager TestSeedStockFidelity::freshStorage()
{
    clearSettings();
    auto *leaked = new QTemporaryDir();
    leaked->setAutoRemove(true);
    if (!leaked->isValid()) {
        return StorageManager();
    }
    m_storageRoots.append(leaked);
    return StorageManager(leaked->path());
}

void TestSeedStockFidelity::stockDefaults_ForBuild_isCorrectShape()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("build"));
    QVERIFY(!r.id.isEmpty());
    // The build Role's permission map mirrors stock opencode's
    // `agent.ts:141-155`: defaults + { question: "allow",
    // plan_enter: "allow" }. The merged shape surfaces as a flat
    // `permission` object on the rendered agent — we verify the
    // overrides alone here.
    const QJsonObject p = r.permissions;
    QCOMPARE(p.value(QStringLiteral("question")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("plan_enter")).toString(),
             QStringLiteral("allow"));
    // defaults are present
    QCOMPARE(p.value(QStringLiteral("*")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("plan_exit")).toString(),
             QStringLiteral("deny"));
    // env-aware read rules
    const QJsonObject read = p.value(QStringLiteral("read")).toObject();
    QCOMPARE(read.value(QStringLiteral("*.env")).toString(),
             QStringLiteral("ask"));
    QCOMPARE(read.value(QStringLiteral("*.env.example")).toString(),
             QStringLiteral("allow"));
}

void TestSeedStockFidelity::stockDefaults_ForPlan_isCorrectShape()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("plan"));
    QVERIFY(!r.id.isEmpty());
    const QJsonObject p = r.permissions;
    QCOMPARE(p.value(QStringLiteral("plan_exit")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("question")).toString(),
             QStringLiteral("allow"));
    // task.general: deny (object form)
    const QJsonObject task = p.value(QStringLiteral("task")).toObject();
    QCOMPARE(task.value(QStringLiteral("general")).toString(),
             QStringLiteral("deny"));
    // edit: { *: "deny", <plans>/...: "allow" }
    const QJsonObject edit = p.value(QStringLiteral("edit")).toObject();
    QCOMPARE(edit.value(QStringLiteral("*")).toString(),
             QStringLiteral("deny"));
    QVERIFY(edit.value(QStringLiteral(".opencode/plans/*.md"))
                .toString() == QLatin1String("allow"));
}

void TestSeedStockFidelity::stockDefaults_ForGeneral_isCorrectShape()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("general"));
    QVERIFY(!r.id.isEmpty());
    const QJsonObject p = r.permissions;
    QCOMPARE(p.value(QStringLiteral("todowrite")).toString(),
             QStringLiteral("deny"));
    QCOMPARE(p.value(QStringLiteral("*")).toString(),
             QStringLiteral("allow"));
}

void TestSeedStockFidelity::stockDefaults_ForExplore_isCorrectShape()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("explore"));
    QVERIFY(!r.id.isEmpty());
    const QJsonObject p = r.permissions;
    // explore overrides *default* with deny + several keys with
    // allow (agent.ts:196-211).
    QCOMPARE(p.value(QStringLiteral("*")).toString(),
             QStringLiteral("deny"));
    QCOMPARE(p.value(QStringLiteral("read")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("grep")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("glob")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("list")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("bash")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("webfetch")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(p.value(QStringLiteral("websearch")).toString(),
             QStringLiteral("allow"));
}

void TestSeedStockFidelity::stockDefaults_ForCompaction_isCorrectShape()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("compaction"));
    QVERIFY(!r.id.isEmpty());
    // compaction: defaults + { *: "deny" } (agent.ts:225-231)
    const QJsonObject p = r.permissions;
    QCOMPARE(p.value(QStringLiteral("*")).toString(),
             QStringLiteral("deny"));
}

void TestSeedStockFidelity::stockDefaults_ForTitle_isCorrectShape()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("title"));
    QVERIFY(!r.id.isEmpty());
    const QJsonObject p = r.permissions;
    QCOMPARE(p.value(QStringLiteral("*")).toString(),
             QStringLiteral("deny"));
}

void TestSeedStockFidelity::stockDefaults_ForSummary_isCorrectShape()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("summary"));
    QVERIFY(!r.id.isEmpty());
    const QJsonObject p = r.permissions;
    QCOMPARE(p.value(QStringLiteral("*")).toString(),
             QStringLiteral("deny"));
}

void TestSeedStockFidelity::versionDefaultsTo_V1_StockFidelity()
{
    clearSettings();
    QCOMPARE(static_cast<int>(StorageManager::SeedVersion::v1_stockFidelity),
             1);
    QCOMPARE(static_cast<int>(StorageManager::readSeedVersion()),
             static_cast<int>(StorageManager::SeedVersion::v1_stockFidelity));
}

void TestSeedStockFidelity::seededBuild_metadataNativeIsTrue()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("build"));
    QVERIFY(!r.id.isEmpty());
    QCOMPARE(r.metadata.value(QStringLiteral("native")).toBool(), true);
}

void TestSeedStockFidelity::seededBuild_permissionsContains_questionAllow()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("build"));
    QCOMPARE(r.permissions.value(QStringLiteral("question")).toString(),
             QStringLiteral("allow"));
}

void TestSeedStockFidelity::seededPlan_modeIsPrimary()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("plan"));
    // Critical D-9 divergence from the v0 fiction seed: stock's plan
    // is mode=primary, not Subagent. test_role_editor_dialog and
    // test_starter_team_apply both depend on this.
    QVERIFY(r.mode == Role::Mode::Primary);
}

void TestSeedStockFidelity::seededPlan_permissionsContains_planExitAllow()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("plan"));
    QCOMPARE(r.permissions.value(QStringLiteral("plan_exit")).toString(),
             QStringLiteral("allow"));
}

void TestSeedStockFidelity::seededPlan_editBlockDeniedExceptPlans()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("plan"));
    const QJsonObject edit = r.permissions.value(QStringLiteral("edit")).toObject();
    QCOMPARE(edit.value(QStringLiteral("*")).toString(),
             QStringLiteral("deny"));
    // The plans-specific pattern must be allowed.
    QVERIFY(edit.value(QStringLiteral(".opencode/plans/*.md"))
                .toString() == QLatin1String("allow"));
}

void TestSeedStockFidelity::seededPlan_taskGeneralIsDeny()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("plan"));
    const QJsonObject task = r.permissions.value(QStringLiteral("task")).toObject();
    QCOMPARE(task.value(QStringLiteral("general")).toString(),
             QStringLiteral("deny"));
}

void TestSeedStockFidelity::seededGeneral_metadataNativeIsTrue()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("general"));
    QCOMPARE(r.metadata.value(QStringLiteral("native")).toBool(), true);
}

void TestSeedStockFidelity::seededGeneral_todowriteIsDeny()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("general"));
    QCOMPARE(r.permissions.value(QStringLiteral("todowrite")).toString(),
             QStringLiteral("deny"));
}

void TestSeedStockFidelity::seededExplore_modeIsSubagent()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("explore"));
    QVERIFY(r.mode == Role::Mode::Subagent);
}

void TestSeedStockFidelity::seededExplore_permissionsRead_andGlob_andGrep_AreAllow()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("explore"));
    QCOMPARE(r.permissions.value(QStringLiteral("read")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(r.permissions.value(QStringLiteral("glob")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(r.permissions.value(QStringLiteral("grep")).toString(),
             QStringLiteral("allow"));
}

void TestSeedStockFidelity::seededExplore_permissionsStarIsDeny()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("explore"));
    QCOMPARE(r.permissions.value(QStringLiteral("*")).toString(),
             QStringLiteral("deny"));
}

void TestSeedStockFidelity::seededHidden_NativeAndHiddenFlagsAreTrue()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    for (const QString &id : {QStringLiteral("compaction"),
                              QStringLiteral("title"),
                              QStringLiteral("summary")}) {
        Role r = storage.loadRole(id);
        QVERIFY2(!r.id.isEmpty(),
                 qPrintable(QStringLiteral("missing role %1").arg(id)));
        QCOMPARE(r.metadata.value(QStringLiteral("native")).toBool(), true);
        QCOMPARE(r.metadata.value(QStringLiteral("hidden")).toBool(), true);
    }
}

void TestSeedStockFidelity::seededCompaction_systemPromptMatchesStockTxt()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("compaction"));
    QVERIFY(r.systemPrompt.isString());
    const QString s = r.systemPrompt.toString();
    // Sanity: stock compaction prompt always opens with "You are an
    // anchored context summarization assistant".
    QVERIFY(s.startsWith(QStringLiteral(
        "You are an anchored context summarization assistant")));
}

void TestSeedStockFidelity::seededTitle_systemPromptMatchesStockTxt()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("title"));
    QVERIFY(r.systemPrompt.isString());
    QVERIFY(r.systemPrompt.toString().startsWith(
        QStringLiteral("You are a title generator.")));
}

void TestSeedStockFidelity::seededSummary_systemPromptMatchesStockTxt()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Role r = storage.loadRole(QStringLiteral("summary"));
    QVERIFY(r.systemPrompt.isString());
    QVERIFY(r.systemPrompt.toString().startsWith(
        QStringLiteral("Summarize what was done in this conversation.")));
}

void TestSeedStockFidelity::seededRolesTotal_IsAtLeastSeven()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    const QList<Role> roles = storage.listRoles();
    // 4 main (build/plan/general/explore) + 3 hidden (compaction/
    // title/summary) = 7. Any new native agent added by stock bumps
    // the count; we assert >= 7 so adding a native always grows the
    // seed without rethinking it.
    QVERIFY2(roles.size() >= 7,
             qPrintable(QStringLiteral("expected >= 7 roles, got %1")
                            .arg(roles.size())));
    for (const QString &must : {QStringLiteral("build"),
                                QStringLiteral("plan"),
                                QStringLiteral("general"),
                                QStringLiteral("explore"),
                                QStringLiteral("compaction"),
                                QStringLiteral("title"),
                                QStringLiteral("summary")}) {
        bool found = false;
        for (const Role &r : roles) {
            if (r.id == must) {
                found = true;
                break;
            }
        }
        QVERIFY2(found,
                 qPrintable(QStringLiteral("missing role id %1").arg(must)));
    }
}

void TestSeedStockFidelity::seededStarterTeam_hasDefaultAgent_SetToBuild()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Team t = storage.loadTeam(QStringLiteral("starter-team"));
    QVERIFY(!t.id.isEmpty());
    QCOMPARE(t.metadata.value(QStringLiteral("default_agent")).toString(),
             QStringLiteral("starter-build"));
}

void TestSeedStockFidelity::seededStarterTeam_bindsExploreSpecialist()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Team t = storage.loadTeam(QStringLiteral("starter-team"));
    bool found = false;
    for (const auto &binding : t.specialists) {
        if (binding.roleId == QLatin1String("explore")
            && binding.specialistId == QLatin1String("starter-explore")) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void TestSeedStockFidelity::seededStarterTeam_bindsAllFourSpecialists()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    Team t = storage.loadTeam(QStringLiteral("starter-team"));
    QCOMPARE(t.specialists.size(), 4);
    QSet<QString> roles;
    for (const auto &binding : t.specialists) {
        roles.insert(binding.roleId);
    }
    QCOMPARE(roles.size(), 4);
    QVERIFY(roles.contains(QStringLiteral("build")));
    QVERIFY(roles.contains(QStringLiteral("plan")));
    QVERIFY(roles.contains(QStringLiteral("general")));
    QVERIFY(roles.contains(QStringLiteral("explore")));
}

void TestSeedStockFidelity::seededSpecialists_countIsAtLeastFour()
{
    auto storage = freshStorage();
    storage.seedDefaultsIfNeeded();
    const QList<Specialist> specs = storage.listSpecialists();
    QVERIFY2(specs.size() >= 4,
             qPrintable(QStringLiteral("expected >= 4 specialists, got %1")
                            .arg(specs.size())));
    QSet<QString> ids;
    for (const Specialist &s : specs) {
        ids.insert(s.id);
    }
    QVERIFY(ids.contains(QStringLiteral("starter-build")));
    QVERIFY(ids.contains(QStringLiteral("starter-plan")));
    QVERIFY(ids.contains(QStringLiteral("starter-general")));
    QVERIFY(ids.contains(QStringLiteral("starter-explore")));
}

QTEST_MAIN(TestSeedStockFidelity)
#include "test_seed_stock_fidelity.moc"
