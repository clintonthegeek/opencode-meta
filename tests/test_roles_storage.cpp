// Smoke test for StorageManager create/delete Role round-trip.
//
// Mirrors test_teams_storage.cpp but exercises the Roles side of the
// PARADIGM storage layer added for P0-2 (Centralize delete operations):
// saveRole persists under roles/<id>.json, deleteRole removes that file,
// and listRoles no longer surfaces it.

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

QTEST_MAIN(TestRolesStorage)
#include "test_roles_storage.moc"
