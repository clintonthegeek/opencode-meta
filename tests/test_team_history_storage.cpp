// Tests for StorageManager's Team version-history API (ROADMAP P3-3).
//
// Covers the full CRUD chain that the Team editor relies on:
//   * saveTeamWithSnapshot writes the prior team as a snapshot
//     before overwriting the live file (so old state is recoverable).
//   * listTeamVersions filters strictly by teamId and is sorted
//     newest-first, and lets unrelated team files coexist on disk.
//   * loadTeamVersion returns the snapshot data verbatim.
//   * revertTeamToVersion restores the picked snapshot as the live
//     Team AND emits a new snapshot of the team we just clobbered,
//     so reverting is itself reversible.
//   * deleteTeam also cleans up history files for that team.
//   * Snapshots persist the parent chain (parent_version_id) so a
//     viewer can walk the timeline backwards.

#include <QTest>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "models/Team.h"
#include "models/TeamVersion.h"
#include "storage/StorageManager.h"

class TestTeamHistoryStorage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void firstSaveDoesNotCreateSnapshot();
    void subsequentSavesChainHeadAndIncrementFiles();
    void listVersionsFiltersByTeamId();
    void snapshotsPersistTimestampAndReason();
    void revertRestoresSnapshotAndSnapshotsCurrent();
    void revertFailsForUnknownSnapshot();
    void revertFailsForEmptyInputs();
    void deleteTeamCleansHistory();

private:
    static Team seedTeam(const QString &id, const QString &name, const QString &version);

    QTemporaryDir m_tmpRoot;
};

Team TestTeamHistoryStorage::seedTeam(const QString &id,
                                      const QString &name,
                                      const QString &version)
{
    Team t;
    t.id          = id;
    t.name        = name;
    t.description = QStringLiteral("history test team %1").arg(version);
    t.version     = version;
    t.parentTeamId = QString();
    t.primarySpecialistIds.append(QStringLiteral("spec-a"));
    {
        Team::SpecialistBinding b;
        b.roleId = QStringLiteral("build");
        b.specialistId = QStringLiteral("spec-a");
        t.specialists.append(b);
    }
    return t;
}

void TestTeamHistoryStorage::initTestCase()
{
    QVERIFY(m_tmpRoot.isValid());
}

void TestTeamHistoryStorage::firstSaveDoesNotCreateSnapshot()
{
    // ROADMAP P3-3: the very first save of a Team id should not write
    // a snapshot file — there is no prior state to capture.
    StorageManager storage(m_tmpRoot.path());
    const Team first = seedTeam(QStringLiteral("t-first"),
                                QStringLiteral("First Team"),
                                QStringLiteral("1.0.0"));
    QVERIFY(storage.saveTeamWithSnapshot(first,
                                         QString::fromLatin1(TeamVersion::kReasonAutoCreate),
                                         QStringLiteral("seed")));

    QCOMPARE(storage.listTeamVersions(first.id).size(), 0);
    QCOMPARE(storage.listTeams().size(), 1);

    // The live Team file should hold the data we just wrote.
    const Team loaded = storage.loadTeam(first.id);
    QCOMPARE(loaded.name, first.name);
    QCOMPARE(loaded.version, first.version);
}

void TestTeamHistoryStorage::subsequentSavesChainHeadAndIncrementFiles()
{
    // Two saves after the head exists should each record a snapshot
    // of the prior state, with the most recent snapshot as the head
    // and an N -> N-1 parent chain.
    StorageManager storage(m_tmpRoot.path());
    const QString id = QStringLiteral("t-chain");

    const Team v1 = seedTeam(id, QStringLiteral("Chain Team"), QStringLiteral("1.0.0"));
    QVERIFY(storage.saveTeamWithSnapshot(v1,
                                         QString::fromLatin1(TeamVersion::kReasonAutoCreate),
                                         QStringLiteral("seed")));
    QCOMPARE(storage.listTeamVersions(id).size(), 0);

    // Save v2 — should snapshot v1, then write v2 to live.
    Team v2 = v1;
    v2.version = QStringLiteral("1.1.0");
    v2.name = QStringLiteral("Chain Team v2");
    QVERIFY(storage.saveTeamWithSnapshot(v2,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit),
                                         QStringLiteral("tweak prompt")));
    QList<TeamVersion> chain = storage.listTeamVersions(id);
    QCOMPARE(chain.size(), 1);
    QCOMPARE(chain.first().team.version, QStringLiteral("1.0.0"));
    QCOMPARE(chain.first().reason, QString::fromLatin1(TeamVersion::kReasonAutoEdit));
    QVERIFY(chain.first().parentVersionId.isEmpty());

    // Save v3 — should snapshot v2 (chain now 2 deep), with parent_version_id
    // pointing at v2's snapshot id.
    Team v3 = v2;
    v3.version = QStringLiteral("1.2.0");
    v3.name = QStringLiteral("Chain Team v3");
    QVERIFY(storage.saveTeamWithSnapshot(v3,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit),
                                         QStringLiteral("reorder")));

    chain = storage.listTeamVersions(id);
    QCOMPARE(chain.size(), 2);

    // Sorted newest first: head is v2's snapshot (recorded just now).
    const TeamVersion head = chain.first();
    const TeamVersion prior = chain.at(1);
    QCOMPARE(head.team.version, QStringLiteral("1.1.0"));
    QCOMPARE(prior.team.version, QStringLiteral("1.0.0"));
    QCOMPARE(head.parentVersionId, prior.id);

    // And the live file holds v3.
    const Team liveNow = storage.loadTeam(id);
    QCOMPARE(liveNow.version, QStringLiteral("1.2.0"));
    QCOMPARE(liveNow.name, QStringLiteral("Chain Team v3"));
}

void TestTeamHistoryStorage::listVersionsFiltersByTeamId()
{
    // Two Teams, each with their own version chain. listVersions
    // should never bleed ids across teams.
    StorageManager storage(m_tmpRoot.path());

    Team a = seedTeam(QStringLiteral("t-a"), QStringLiteral("A"), QStringLiteral("1.0.0"));
    QVERIFY(storage.saveTeamWithSnapshot(a,
                                         QString::fromLatin1(TeamVersion::kReasonAutoCreate)));
    Team b = seedTeam(QStringLiteral("t-b"), QStringLiteral("B"), QStringLiteral("1.0.0"));
    QVERIFY(storage.saveTeamWithSnapshot(b,
                                         QString::fromLatin1(TeamVersion::kReasonAutoCreate)));

    // Two edits on a, one on b.
    Team a2 = a;
    a2.version = QStringLiteral("1.0.1");
    QVERIFY(storage.saveTeamWithSnapshot(a2,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit)));
    Team a3 = a2;
    a3.version = QStringLiteral("1.0.2");
    QVERIFY(storage.saveTeamWithSnapshot(a3,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit)));

    Team b2 = b;
    b2.version = QStringLiteral("1.1.0");
    QVERIFY(storage.saveTeamWithSnapshot(b2,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit)));

    QCOMPARE(storage.listTeamVersions(QStringLiteral("t-a")).size(), 2);
    QCOMPARE(storage.listTeamVersions(QStringLiteral("t-b")).size(), 1);
    QVERIFY(storage.listTeamVersions(QStringLiteral("not-there")).isEmpty());
}

void TestTeamHistoryStorage::snapshotsPersistTimestampAndReason()
{
    StorageManager storage(m_tmpRoot.path());

    const Team first = seedTeam(QStringLiteral("t-time"),
                                QStringLiteral("Time Team"),
                                QStringLiteral("1.0.0"));
    QVERIFY(storage.saveTeamWithSnapshot(first,
                                         QString::fromLatin1(TeamVersion::kReasonAutoCreate)));

    Team edited = first;
    edited.version = QStringLiteral("2.0.0");
    edited.name = QStringLiteral("Time Team Renamed");
    QVERIFY(storage.saveTeamWithSnapshot(edited,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit),
                                         QStringLiteral("rename")));

    const QList<TeamVersion> chain = storage.listTeamVersions(QStringLiteral("t-time"));
    QCOMPARE(chain.size(), 1);
    const TeamVersion snap = chain.first();
    QVERIFY(snap.timestampUtc.isValid());
    QCOMPARE(snap.reason, QString::fromLatin1(TeamVersion::kReasonAutoEdit));
    QCOMPARE(snap.note, QStringLiteral("rename"));
    QCOMPARE(snap.team.version, QStringLiteral("1.0.0"));
    QCOMPARE(snap.team.name, QStringLiteral("Time Team"));

    // Round-trip through JSON so we can lock down the on-disk format.
    const QJsonObject obj = snap.toJson();
    QVERIFY(obj.value(QStringLiteral("schema")).toString().startsWith(QStringLiteral("opencode-meta-team")));
    QCOMPARE(obj.value(QStringLiteral("team_id")).toString(), QStringLiteral("t-time"));
    QVERIFY(obj.value(QStringLiteral("timestamp_utc")).isString());
    QVERIFY(obj.contains(QStringLiteral("team")));
}

void TestTeamHistoryStorage::revertRestoresSnapshotAndSnapshotsCurrent()
{
    StorageManager storage(m_tmpRoot.path());

    Team v1 = seedTeam(QStringLiteral("t-restore"),
                       QStringLiteral("Restore Team"),
                       QStringLiteral("1.0.0"));
    QVERIFY(storage.saveTeamWithSnapshot(v1,
                                         QString::fromLatin1(TeamVersion::kReasonAutoCreate)));

    Team v2 = v1;
    v2.version = QStringLiteral("2.0.0");
    v2.name = QStringLiteral("Restore Team v2");
    v2.description = QStringLiteral("promoted specialists");
    QVERIFY(storage.saveTeamWithSnapshot(v2,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit),
                                         QStringLiteral("promote")));

    // Capture the snapshot id from the only existing snapshot (the
    // pre-v2 head) so we can target it explicitly.
    QList<TeamVersion> chain = storage.listTeamVersions(QStringLiteral("t-restore"));
    QCOMPARE(chain.size(), 1);
    const QString snapshotId = chain.first().id;
    QVERIFY(!snapshotId.isEmpty());

    const StorageManager::RevertResult result =
        storage.revertTeamToVersion(QStringLiteral("t-restore"),
                                    snapshotId,
                                    QStringLiteral("oops, undo"));
    QVERIFY2(result.ok, qPrintable(QStringLiteral("revert failed: ") + result.errorString));
    QCOMPARE(result.restored.team.version, QStringLiteral("1.0.0"));

    // After revert: the live Team is back at v1.
    const Team live = storage.loadTeam(QStringLiteral("t-restore"));
    QCOMPARE(live.version, QStringLiteral("1.0.0"));
    QCOMPARE(live.name, QStringLiteral("Restore Team"));

    // And there must be a new head snapshot of the v2 state we just
    // clobbered, with reason "revert:<id>" and parent set to that id.
    chain = storage.listTeamVersions(QStringLiteral("t-restore"));
    QCOMPARE(chain.size(), 2);
    const TeamVersion newHead = chain.first();
    QCOMPARE(newHead.team.version, QStringLiteral("2.0.0"));
    QCOMPARE(newHead.reason,
             QStringLiteral("revert:%1").arg(snapshotId));
    QCOMPARE(newHead.note, QStringLiteral("oops, undo"));
    const TeamVersion oldHead = chain.at(1);
    QCOMPARE(newHead.parentVersionId, oldHead.id);
    QCOMPARE(oldHead.id, snapshotId);
}

void TestTeamHistoryStorage::revertFailsForUnknownSnapshot()
{
    StorageManager storage(m_tmpRoot.path());
    Team t = seedTeam(QStringLiteral("t-missing-snap"),
                      QStringLiteral("Missing Snap"),
                      QStringLiteral("1.0.0"));
    QVERIFY(storage.saveTeamWithSnapshot(t,
                                         QString::fromLatin1(TeamVersion::kReasonAutoCreate)));

    const StorageManager::RevertResult result =
        storage.revertTeamToVersion(QStringLiteral("t-missing-snap"),
                                    QStringLiteral("tv-does-not-exist"));
    QVERIFY(!result.ok);
    QVERIFY(!result.errorString.isEmpty());

    // Live Team must be untouched.
    const Team live = storage.loadTeam(QStringLiteral("t-missing-snap"));
    QCOMPARE(live.version, t.version);
    QCOMPARE(live.name, t.name);
}

void TestTeamHistoryStorage::revertFailsForEmptyInputs()
{
    StorageManager storage(m_tmpRoot.path());
    {
        const StorageManager::RevertResult r =
            storage.revertTeamToVersion(QString(), QStringLiteral("tv-x"));
        QVERIFY(!r.ok);
    }
    {
        const StorageManager::RevertResult r =
            storage.revertTeamToVersion(QStringLiteral("t-x"), QString());
        QVERIFY(!r.ok);
    }
}

void TestTeamHistoryStorage::deleteTeamCleansHistory()
{
    StorageManager storage(m_tmpRoot.path());
    Team v1 = seedTeam(QStringLiteral("t-deleteme"),
                       QStringLiteral("Delete Me"),
                       QStringLiteral("1.0.0"));
    QVERIFY(storage.saveTeamWithSnapshot(v1,
                                         QStringLiteral("create")));

    Team v2 = v1;
    v2.version = QStringLiteral("2.0.0");
    QVERIFY(storage.saveTeamWithSnapshot(v2,
                                         QStringLiteral("edit")));
    Team v3 = v2;
    v3.version = QStringLiteral("3.0.0");
    QVERIFY(storage.saveTeamWithSnapshot(v3,
                                         QStringLiteral("edit")));

    QCOMPARE(storage.listTeamVersions(QStringLiteral("t-deleteme")).size(), 2);

    QVERIFY(storage.deleteTeam(QStringLiteral("t-deleteme")));
    QCOMPARE(storage.listTeamVersions(QStringLiteral("t-deleteme")).size(), 0);

    // Confirm the snapshot files are physically gone on disk, not
    // just hidden behind an empty list.
    QDir dir(m_tmpRoot.filePath(QStringLiteral("team-versions")));
    const QStringList remaining = dir.entryList(
        QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &name : remaining) {
        QVERIFY2(!name.startsWith(QStringLiteral("tv-t-deleteme")),
                 qPrintable(QStringLiteral("orphan snapshot: ") + name));
    }
}

QTEST_MAIN(TestTeamHistoryStorage)
#include "test_team_history_storage.moc"
