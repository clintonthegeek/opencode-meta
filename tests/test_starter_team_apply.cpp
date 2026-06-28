// End-to-end smoke test for applying the seeded "Starter Team" to a project.

#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

#include "storage/StorageManager.h"

class TestStarterTeamApply : public QObject
{
    Q_OBJECT

private slots:
    void applyStarterTeam_writesConfigTrialAndProjectsRecord();
};

void TestStarterTeamApply::applyStarterTeam_writesConfigTrialAndProjectsRecord()
{
    StorageManager storage; // uses default ~/.opencode-meta root
    storage.ensureRoot();
    storage.seedDefaultsIfNeeded();

    // Locate the seeded Starter Team.
    const QList<Team> teams = storage.listTeams();
    QString starterTeamId;
    for (const Team &team : teams) {
        if (team.id == QStringLiteral("starter-team")) {
            starterTeamId = team.id;
            break;
        }
    }
    QVERIFY2(!starterTeamId.isEmpty(), "starter-team not found under ~/.opencode-meta/teams");

    // Use a stable temporary project directory so we can inspect it manually if needed.
    const QString projectPath = QStringLiteral("/tmp/opencode-meta-qt-starter-project");
    QDir fsRoot;
    QVERIFY(fsRoot.mkpath(projectPath));

    const bool ok = storage.applyTeamToProject(projectPath, starterTeamId);
    QVERIFY2(ok, "applyTeamToProject returned false for starter-team");

    // Verify that project-local opencode.json was written with the expected shape.
    const QString configPath = QDir(projectPath).filePath(QStringLiteral("opencode.json"));
    QFile configFile(configPath);
    QVERIFY2(configFile.exists(), "opencode.json was not written to project directory");
    QVERIFY(configFile.open(QIODevice::ReadOnly));

    const QJsonDocument configDoc = QJsonDocument::fromJson(configFile.readAll());
    QVERIFY(configDoc.isObject());
    const QJsonObject configObj = configDoc.object();

    QCOMPARE(configObj.value(QStringLiteral("$schema")).toString(),
             QStringLiteral("https://opencode.ai/config.json"));
    QCOMPARE(configObj.value(QStringLiteral("default_agent")).toString(),
             QStringLiteral("build"));

    const QJsonObject agents = configObj.value(QStringLiteral("agent")).toObject();
    QVERIFY2(agents.contains(QStringLiteral("build")), "agent map is missing 'build'");
    QVERIFY2(agents.contains(QStringLiteral("plan")), "agent map is missing 'plan'");

    const QJsonObject buildAgent = agents.value(QStringLiteral("build")).toObject();
    QCOMPARE(buildAgent.value(QStringLiteral("model")).toString(),
             QStringLiteral("anthropic/claude-sonnet-4-6"));

    const QString buildPrompt = buildAgent.value(QStringLiteral("prompt")).toString();
    QVERIFY2(buildPrompt.contains(QStringLiteral("You are the primary Build agent in an OpenCode workspace.")),
             "build agent prompt does not contain the seeded Build role text");

    // Verify that a Trial JSON was recorded under the storage root.
    const QString storageRoot = QDir::homePath() + QStringLiteral("/.opencode-meta");
    const QString trialsPath = QDir(storageRoot).filePath(QStringLiteral("trials"));
    QDir trialsDir(trialsPath);
    QVERIFY2(trialsDir.exists(), "trials directory does not exist under storage root");

    const QStringList trialFiles = trialsDir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    QVERIFY2(!trialFiles.isEmpty(), "no Trial JSON files were written");

    // Verify that projects.json contains an association for this project.
    const QString projectsFilePath = QDir(storageRoot).filePath(QStringLiteral("projects.json"));
    QFile projectsFile(projectsFilePath);
    QVERIFY2(projectsFile.exists(), "projects.json was not written");
    QVERIFY(projectsFile.open(QIODevice::ReadOnly));

    const QJsonDocument projectsDoc = QJsonDocument::fromJson(projectsFile.readAll());
    QVERIFY(projectsDoc.isArray());
    const QJsonArray projectsArray = projectsDoc.array();
    QVERIFY(!projectsArray.isEmpty());

    bool found = false;
    for (const QJsonValue &v : projectsArray) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject obj = v.toObject();
        if (QDir::cleanPath(obj.value(QStringLiteral("path")).toString()) ==
            QDir::cleanPath(projectPath)) {
            found = true;
            QCOMPARE(obj.value(QStringLiteral("team_id")).toString(), starterTeamId);
            QVERIFY(!obj.value(QStringLiteral("last_trial_id")).toString().isEmpty());
        }
    }
    QVERIFY2(found, "projects.json does not contain a record for the starter project path");
}

QTEST_MAIN(TestStarterTeamApply)
#include "test_starter_team_apply.moc"
