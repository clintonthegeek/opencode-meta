// Smoke test for StorageManager create/delete Team round-trip.
//
// Exercises the storage-layer additions for Phase F4 (TeamsWidget
// Create + Delete Team): saveTeam persists a Team under teams/<id>.json,
// deleteTeam removes that file, and listTeams no longer surfaces the
// deleted Team.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "storage/StorageManager.h"

class TestTeamsStorage : public QObject
{
    Q_OBJECT

private slots:
    void createAndDeleteTeam();
};

void TestTeamsStorage::createAndDeleteTeam()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    StorageManager storage(tmpRoot.path());

    // Sanity: an empty storage has no teams.
    QCOMPARE(storage.listTeams().size(), 0);

    Team alpha;
    alpha.id = QStringLiteral("alpha-team");
    alpha.name = QStringLiteral("Alpha Team");
    alpha.description = QStringLiteral("first Team");
    alpha.version = QStringLiteral("0.1.0");
    QVERIFY(storage.saveTeam(alpha));

    // The Team JSON file should now exist on disk.
    const QString alphaFile = tmpRoot.filePath(QStringLiteral("teams/alpha-team.json"));
    QVERIFY2(QFile::exists(alphaFile), "alpha-team.json was not written under teams/");

    // And listTeams surfaces it.
    {
        const QList<Team> listed = storage.listTeams();
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().id, QStringLiteral("alpha-team"));
        QCOMPARE(listed.first().name, QStringLiteral("Alpha Team"));
    }

    // deleteTeam removes the file and advances the list.
    QVERIFY(storage.deleteTeam(QStringLiteral("alpha-team")));
    QVERIFY2(!QFile::exists(alphaFile), "deleteTeam did not remove alpha-team.json");
    QCOMPARE(storage.listTeams().size(), 0);

    // Loading after delete should report an empty Team.
    {
        const Team ghost = storage.loadTeam(QStringLiteral("alpha-team"));
        QVERIFY(ghost.id.isEmpty());
    }

    // Deleting again is idempotent at the storage level (returns false
    // because there is nothing left to remove).
    QVERIFY(!storage.deleteTeam(QStringLiteral("alpha-team")));

    // Recreate a Team with a colliding id to prove saveTeam is happy
    // after the previous one has been removed.
    Team recreated = alpha;
    QVERIFY(storage.saveTeam(recreated));
    QVERIFY(QFile::exists(alphaFile));
    QCOMPARE(storage.listTeams().size(), 1);

    // Finally, deleteTeam on an empty/missing id must refuse cleanly.
    QVERIFY(!storage.deleteTeam(QString()));
    QVERIFY(!storage.deleteTeam(QStringLiteral("never-existed")));

    // Verify the saved JSON is a well-formed Team document with the
    // expected top-level keys spec'd by docs/PARADIGM.md (§2.3 Team).
    QFile file(alphaFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("id")).toString(), QStringLiteral("alpha-team"));
    QCOMPARE(obj.value(QStringLiteral("name")).toString(), QStringLiteral("Alpha Team"));
    QCOMPARE(obj.value(QStringLiteral("description")).toString(), QStringLiteral("first Team"));
    QCOMPARE(obj.value(QStringLiteral("version")).toString(), QStringLiteral("0.1.0"));
}

QTEST_MAIN(TestTeamsStorage)
#include "test_teams_storage.moc"
