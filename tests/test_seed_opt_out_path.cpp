// tests/test_seed_opt_out_path.cpp
// Phase D4-3 / D-7 escape hatch.
//
// Locks the v0-fiction opt-out path so that any user (or future
// runtime regression) who wants the pre-D1 approximation can keep it.
// With QSettings key `settings/seed_stock_defaults = false`, a fresh
// launch with empty storage still seeds — but writes:
//   * `seed_version = v0_legacyFiction`
//   * a Plan Role with `mode = Subagent` (the v0 fiction shape)
//   * no `metadata.native` flag on any Role
// This is the explicit D-7 escape-hatch preserved for any rare user
// that prefers to run stock opencode on top of the older opencode-meta
// shell semantics.
//
// Three slots lock the contract:
//
//   1. optOutSeeds_v0_legacyFiction
//      The seed runs end-to-end but writes seed_version
//      `v0_legacyFiction` (NOT v1_stockFidelity).
//
//   2. optOut_plan_isSubagent
//      The Plan Role ships with mode = Subagent (v0 fiction shape),
//      not the stock-aligned Primary.
//
//   3. optOut_role_notMarkedNative
//      No Role carries `metadata.native = true`. The "native"
//      surface is entirely absent so users picking the opt-out path
//      can never accidentally hit the D-10 native-lock behaviour.

#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include "models/Role.h"
#include "storage/StorageManager.h"

class TestSeedOptOutPath : public QObject
{
    Q_OBJECT

private slots:
    void optOutSeeds_v0_legacyFiction();
    void optOut_plan_isSubagent();
    void optOut_role_notMarkedNative();
};

void TestSeedOptOutPath::optOutSeeds_v0_legacyFiction()
{
    if (QCoreApplication::instance() != nullptr) {
        QSettings s;
        s.setValue(QStringLiteral("settings/seed_stock_defaults"),
                   QVariant(false));
        s.sync();
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.path());
    storage.seedDefaultsIfNeeded();

    QCOMPARE(static_cast<int>(StorageManager::readSeedVersion()),
             static_cast<int>(StorageManager::SeedVersion::v0_legacyFiction));

    // Cleanup so a following test that wants the default ON starts
    // from a clean QSettings slate.
    if (QCoreApplication::instance() != nullptr) {
        QSettings s;
        s.remove(QStringLiteral("settings/seed_stock_defaults"));
        s.sync();
    }
}

void TestSeedOptOutPath::optOut_plan_isSubagent()
{
    if (QCoreApplication::instance() != nullptr) {
        QSettings s;
        s.setValue(QStringLiteral("settings/seed_stock_defaults"),
                   QVariant(false));
        s.sync();
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.path());
    storage.seedDefaultsIfNeeded();
    Role plan = storage.loadRole(QStringLiteral("plan"));
    QVERIFY(!plan.id.isEmpty());
    QCOMPARE(plan.mode, Role::Mode::Subagent);

    if (QCoreApplication::instance() != nullptr) {
        QSettings s;
        s.remove(QStringLiteral("settings/seed_stock_defaults"));
        s.sync();
    }
}

void TestSeedOptOutPath::optOut_role_notMarkedNative()
{
    if (QCoreApplication::instance() != nullptr) {
        QSettings s;
        s.setValue(QStringLiteral("settings/seed_stock_defaults"),
                   QVariant(false));
        s.sync();
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.path());
    storage.seedDefaultsIfNeeded();

    // All three legacy fiction Roles (build, plan, general) must
    // NOT carry `metadata.native = true`. The opt-out path is the
    // exact pre-D1 behaviour: roles are user-named Roles with no
    // stock annotations.
    for (const QString &id : {QStringLiteral("build"),
                              QStringLiteral("plan"),
                              QStringLiteral("general")}) {
        const Role r = storage.loadRole(id);
        QVERIFY(!r.id.isEmpty());
        QCOMPARE(r.metadata.value(QStringLiteral("native")).toBool(),
                 false);
    }
    // And the opt-out path did NOT seed the new agents.
    QCOMPARE(storage.listRoles().size(), 3);

    if (QCoreApplication::instance() != nullptr) {
        QSettings s;
        s.remove(QStringLiteral("settings/seed_stock_defaults"));
        s.sync();
    }
}

QTEST_MAIN(TestSeedOptOutPath)
#include "test_seed_opt_out_path.moc"
