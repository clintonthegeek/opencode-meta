// tests/test_legacy_storage_unaffected_by_seed.cpp
// Phase D4-2 / §7 migration contract.
//
// Locks the safety rail:
//   A storage root that pre-exists the D1 seed (i.e. one written by
//   a pre-D1 build, or by a fresh v0 fiction opt-out) is NEVER
//   overwritten by a later call to seedDefaultsIfNeeded.
//
// The plan documents this contract explicitly (§7 item 1): "Existing
// user data is sacred. seedDefaultsIfNeeded's early-out at
// StorageManager.cpp:924 (`if (!rolesEmpty && !teamsEmpty) return;`)
// keeps the contract." Three slots lock the contract down:
//
//   1. legacyBuild_Role_isNotOverwritten
//      A pre-existing `roles/build.json` carrying the v0 fiction
//      shape (mode = Subagent, no metadata.native, no permission
//      defaults) is left untouched after a fresh seedDefaultsIfNeeded.
//
//   2. legacyStorage_DoesNotBumpSeedVersion
//      The seed_version QSettings key is NOT bumped when storage
//      already has user data, even when storage_versions are stale.
//
//   3. legacyStorage_DoesNotAddNativeRoles
//      The seed does not inject the new native agents
//      (explore / hidden primaries) into a legacy storage root even
//      though users would benefit from those agents. Adding silently
//      would violate the "existing user data is sacred" rule and is
//      the surface that flips the D-12 reset toggle.

#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include "models/Role.h"
#include "models/Team.h"
#include "storage/StorageManager.h"

class TestLegacyStorageUnaffectedBySeed : public QObject
{
    Q_OBJECT

private slots:
    void legacyBuild_Role_isNotOverwritten();
    void legacyStorage_DoesNotBumpSeedVersion();
    void legacyStorage_DoesNotAddNativeRoles();
};

// Helper: write a "v0 fiction" build Role JSON to the storage root
// matching what StorageManager::seedDefaultsIfNeeded produced before
// Phase D1. The file is intentionally Subagent-mode (a value the new
// stock seed never sets) so any later overwrite is observable.
void writeLegacyBuildRole(const QString &rootPath)
{
    QDir(rootPath).mkpath(QStringLiteral("roles"));
    QJsonObject legacyBuild;
    legacyBuild.insert(QStringLiteral("id"), QStringLiteral("build"));
    legacyBuild.insert(QStringLiteral("name"), QStringLiteral("Build"));
    legacyBuild.insert(QStringLiteral("description"),
                       QStringLiteral("(legacy fiction) Build agent."));
    legacyBuild.insert(QStringLiteral("system_prompt"),
                       QStringLiteral("(legacy fiction) stub build body."));
    legacyBuild.insert(QStringLiteral("mode"), QStringLiteral("subagent"));
    legacyBuild.insert(QStringLiteral("readOnly"), false);
    legacyBuild.insert(QStringLiteral("permissions"), QJsonObject());

    QFile f(QDir(rootPath).filePath(QStringLiteral("roles/build.json")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(legacyBuild).toJson(QJsonDocument::Indented));
    f.close();
}

void clearSettings()
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

void TestLegacyStorageUnaffectedBySeed::legacyBuild_Role_isNotOverwritten()
{
    clearSettings();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.path());

    // Pre-populate the storage with a v0 fiction build Role so the
    // seed's rolesEmpty branch is FALSE.
    writeLegacyBuildRole(dir.path());

    // Sanity: the seed should NOT touch our pre-existing file.
    storage.seedDefaultsIfNeeded();

    const Role r = storage.loadRole(QStringLiteral("build"));
    QVERIFY2(!r.id.isEmpty(),
             qPrintable(QStringLiteral("could not load build role")));
    // v0 fiction kept the build role as Subagent. The new stock-
    // aligned seed sets build.mode = Primary. If the legacy role
    // is still Subagent we know the seed did not overwrite.
    QCOMPARE(r.mode, Role::Mode::Subagent);
    // And it did not carry the stock permission defaults — the
    // legacy role had no permission keys (empty Role::permissions)
    // and the seeded stock build has full defaults.
    // A simple true-false flag: if metadata.native is true we
    // know the new seeder touched this Role.
    QCOMPARE(r.metadata.value(QStringLiteral("native")).toBool(), false);
    // Description round-trips our marker, defeating metadata
    // overwrite by the new seeder.
    QVERIFY(r.description.startsWith(QStringLiteral("(legacy fiction)")));
}

void TestLegacyStorageUnaffectedBySeed::legacyStorage_DoesNotBumpSeedVersion()
{
    clearSettings();
    // Pre-set seed_version to a deliberate v0 marker so we can
    // detect any bump after seedDefaultsIfNeeded.
    if (QCoreApplication::instance() != nullptr) {
        QSettings s;
        s.setValue(QStringLiteral("storage/seed_version"),
                   static_cast<int>(StorageManager::SeedVersion::v0_legacyFiction));
        s.sync();
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.path());
    writeLegacyBuildRole(dir.path());

    storage.seedDefaultsIfNeeded();

    // The seed version we read back must equal the pre-set value,
    // not the new v1_stockFidelity the seeder would otherwise write.
    const auto v = StorageManager::readSeedVersion();
    QCOMPARE(static_cast<int>(v),
             static_cast<int>(StorageManager::SeedVersion::v0_legacyFiction));
}

void TestLegacyStorageUnaffectedBySeed::legacyStorage_DoesNotAddNativeRoles()
{
    clearSettings();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.path());
    writeLegacyBuildRole(dir.path());

    storage.seedDefaultsIfNeeded();

    // The legacy storage root had exactly one Role (build). The
    // stock-aligned seed produces 7 native agents, but the early-
    // out at StorageManager.cpp:924 (and the per-branch
    // `if (rolesEmpty)` guard) means a legacy storage NEVER
    // receives the new agents automatically. Users opt in via
    // Settings → Seeding → Reset Storage.
    const QList<Role> roles = storage.listRoles();
    QCOMPARE(roles.size(), 1);
    QCOMPARE(roles.first().id, QStringLiteral("build"));
    // And it is still the legacy fiction (mode=Subagent, no metadata).
    QCOMPARE(roles.first().mode, Role::Mode::Subagent);
    QCOMPARE(roles.first().metadata.value(QStringLiteral("native")).toBool(),
             false);
}

QTEST_MAIN(TestLegacyStorageUnaffectedBySeed)
#include "test_legacy_storage_unaffected_by_seed.moc"
