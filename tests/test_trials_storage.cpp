// Smoke test for StorageManager create/delete Trial round-trip.
//
// Mirrors test_teams_storage.cpp but exercises the Trials side of the
// PARADIGM storage layer added for P0-2 (Centralize delete operations):
// saveTrial persists under trials/<id>.json, deleteTrial removes that file,
// and listTrials no longer surfaces the deleted Trial.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDateTime>

#include "storage/StorageManager.h"

class TestTrialsStorage : public QObject
{
    Q_OBJECT

private slots:
    void createAndDeleteTrial();
};

void TestTrialsStorage::createAndDeleteTrial()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    StorageManager storage(tmpRoot.path());

    QCOMPARE(storage.listTrials().size(), 0);

    Trial trial;
    trial.id = QStringLiteral("trial-20260628");
    trial.teamId = QStringLiteral("starter-team");
    trial.projectPath = QStringLiteral("/tmp/example-project");
    trial.timestamp = QDateTime::currentDateTimeUtc();
    trial.notes = QStringLiteral("first trial");
    QVERIFY(storage.saveTrial(trial));

    const QString trialFile = tmpRoot.filePath(QStringLiteral("trials/trial-20260628.json"));
    QVERIFY2(QFile::exists(trialFile), "trial-20260628.json was not written under trials/");

    {
        const QList<Trial> listed = storage.listTrials();
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().id, QStringLiteral("trial-20260628"));
        QCOMPARE(listed.first().teamId, QStringLiteral("starter-team"));
    }

    QVERIFY(storage.deleteTrial(QStringLiteral("trial-20260628")));
    QVERIFY2(!QFile::exists(trialFile), "deleteTrial did not remove trial-20260628.json");
    QCOMPARE(storage.listTrials().size(), 0);

    {
        const Trial ghost = storage.loadTrial(QStringLiteral("trial-20260628"));
        QVERIFY(ghost.id.isEmpty());
    }

    QVERIFY(!storage.deleteTrial(QStringLiteral("trial-20260628")));

    Trial recreated = trial;
    QVERIFY(storage.saveTrial(recreated));
    QVERIFY(QFile::exists(trialFile));
    QCOMPARE(storage.listTrials().size(), 1);

    QVERIFY(!storage.deleteTrial(QString()));
    QVERIFY(!storage.deleteTrial(QStringLiteral("never-existed")));

    QFile file(trialFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("id")).toString(), QStringLiteral("trial-20260628"));
    QCOMPARE(obj.value(QStringLiteral("team_id")).toString(), QStringLiteral("starter-team"));
    QCOMPARE(obj.value(QStringLiteral("project_path")).toString(), QStringLiteral("/tmp/example-project"));
}

QTEST_MAIN(TestTrialsStorage)
#include "test_trials_storage.moc"
