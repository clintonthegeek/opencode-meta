// Smoke test for StorageManager create/delete Role round-trip.
//
// Mirrors test_teams_storage.cpp but exercises the Roles side of the
// PARADIGM storage layer added for P0-2 (Centralize delete operations):
// saveRole persists under roles/<id>.json, deleteRole removes that file,
// and listRoles no longer surfaces it.
//
// Phase C3-1 added a `readOnly` flag to `Role`; we round-trip the flag
// through StorageManager here to lock the schema round-trip.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "storage/StorageManager.h"

class TestRolesStorage : public QObject
{
    Q_OBJECT

private slots:
    void createAndDeleteRole();
    void readOnlyRoundTripsThroughStorage();
};

void TestRolesStorage::createAndDeleteRole()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    StorageManager storage(tmpRoot.path());

    QCOMPARE(storage.listRoles().size(), 0);

    Role build;
    build.id = QStringLiteral("build");
    build.name = QStringLiteral("Build");
    build.description = QStringLiteral("primary development role");
    build.systemPrompt = QJsonValue(QStringLiteral("You are the Build agent."));
    build.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(build));

    const QString buildFile = tmpRoot.filePath(QStringLiteral("roles/build.json"));
    QVERIFY2(QFile::exists(buildFile), "build.json was not written under roles/");

    {
        const QList<Role> listed = storage.listRoles();
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().id, QStringLiteral("build"));
        QCOMPARE(listed.first().name, QStringLiteral("Build"));
    }

    QVERIFY(storage.deleteRole(QStringLiteral("build")));
    QVERIFY2(!QFile::exists(buildFile), "deleteRole did not remove build.json");
    QCOMPARE(storage.listRoles().size(), 0);

    {
        const Role ghost = storage.loadRole(QStringLiteral("build"));
        QVERIFY(ghost.id.isEmpty());
    }

    QVERIFY(!storage.deleteRole(QStringLiteral("build")));

    Role recreated = build;
    QVERIFY(storage.saveRole(recreated));
    QVERIFY(QFile::exists(buildFile));
    QCOMPARE(storage.listRoles().size(), 1);

    QVERIFY(!storage.deleteRole(QString()));
    QVERIFY(!storage.deleteRole(QStringLiteral("never-existed")));

    QFile file(buildFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("id")).toString(), QStringLiteral("build"));
    QCOMPARE(obj.value(QStringLiteral("name")).toString(), QStringLiteral("Build"));
    QCOMPARE(obj.value(QStringLiteral("description")).toString(), QStringLiteral("primary development role"));
}

void TestRolesStorage::readOnlyRoundTripsThroughStorage()
{
    // Phase C3-1 / D-5: readOnly flag round-trips through StorageManager.
    // We write a Role with readOnly=true, reload via loadRole(), and
    // assert the flag is preserved verbatim. Then write a Role with
    // readOnly=false and confirm the same.
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    StorageManager storage(tmpRoot.path());

    // True case.
    Role readOnlyRole;
    readOnlyRole.id = QStringLiteral("readonly-coder");
    readOnlyRole.name = QStringLiteral("Read-only Coder");
    readOnlyRole.description = QStringLiteral("Primary, no edit/bash");
    readOnlyRole.systemPrompt = QJsonValue(QStringLiteral("You are a read-only agent."));
    readOnlyRole.mode = Role::Mode::Primary;
    readOnlyRole.readOnly = true;
    QVERIFY2(readOnlyRole.readOnly,
             "the in-memory Role defaults / explicit readOnly=true round-trip in this test fixture");
    QVERIFY(storage.saveRole(readOnlyRole));

    const Role loadedReadOnly = storage.loadRole(QStringLiteral("readonly-coder"));
    QVERIFY2(loadedReadOnly.id == QStringLiteral("readonly-coder"),
             "loadRole should read back the same id");
    QVERIFY2(loadedReadOnly.readOnly,
             "readOnly=true should round-trip through StorageManager; load lost the flag");

    // Re-issue toJson() on the loaded Role to assert the flag is still
    // emitted as a JSON bool after the storage round-trip.
    const QJsonObject jsonAfterLoad = loadedReadOnly.toJson();
    QCOMPARE(jsonAfterLoad.value(QStringLiteral("readOnly")).toBool(), true);

    // False case.
    Role builtinBuild;
    builtinBuild.id = QStringLiteral("build");
    builtinBuild.name = QStringLiteral("Build");
    builtinBuild.description = QStringLiteral("Primary build role");
    builtinBuild.systemPrompt = QJsonValue(QStringLiteral("You are the primary Build agent."));
    builtinBuild.mode = Role::Mode::Primary;
    builtinBuild.readOnly = false;
    QVERIFY(storage.saveRole(builtinBuild));

    const Role loadedWriteable = storage.loadRole(QStringLiteral("build"));
    QVERIFY(!loadedWriteable.readOnly);

    // Default-constructed Role (no readOnly assignment) must also be
    // false (the C3-1 spec'd default).
    Role defaultRole;
    QCOMPARE(defaultRole.readOnly, false);
}

QTEST_MAIN(TestRolesStorage)
#include "test_roles_storage.moc"
