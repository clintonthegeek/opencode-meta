#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDir>

#include "storage/StorageManager.h"

class TestApplyTeam : public QObject
{
    Q_OBJECT

private slots:
    void applyTeamToProject_writesConfigAndTrial();
};

void TestApplyTeam::applyTeamToProject_writesConfigAndTrial()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    const QString storageRoot = tmpRoot.path();
    StorageManager storage(storageRoot);

    // Define and persist a simple Role.
    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.name = QStringLiteral("Build");
    buildRole.description = QStringLiteral("Primary build role");
    buildRole.systemPrompt = QJsonValue(QStringLiteral("You are the primary build agent."));
    buildRole.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(buildRole));

    // Define and persist a Specialist bound to the Role.
    Specialist spec;
    spec.id = QStringLiteral("spec-build-1");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    QVERIFY(storage.saveSpecialist(spec));

    // Define and persist a Team wiring the Role to the Specialist.
    Team team;
    team.id = QStringLiteral("team-apply-1");
    team.name = QStringLiteral("Apply Team Test");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding bind;
    bind.roleId = QStringLiteral("build");
    bind.specialistId = spec.id;
    team.specialists.append(bind);
    QVERIFY(storage.saveTeam(team));

    // Create a fake project directory inside the temporary root.
    const QString projectPath = tmpRoot.filePath(QStringLiteral("project"));
    QDir rootDir(tmpRoot.path());
    QVERIFY(rootDir.mkpath(QStringLiteral("project")));

    // Exercise the helper.
    const bool ok = storage.applyTeamToProject(projectPath, team.id);
    QVERIFY(ok);

    // Verify that a project-local opencode.json was written.
    const QString configPath = QDir(projectPath).filePath(QStringLiteral("opencode.json"));
    QFile configFile(configPath);
    QVERIFY(configFile.exists());
    QVERIFY(configFile.open(QIODevice::ReadOnly));
    const QJsonDocument configDoc = QJsonDocument::fromJson(configFile.readAll());
    QVERIFY(configDoc.isObject());
    const QJsonObject configObj = configDoc.object();

    QCOMPARE(configObj.value(QStringLiteral("$schema")).toString(),
             QStringLiteral("https://opencode.ai/config.json"));

    const QJsonObject agents = configObj.value(QStringLiteral("agent")).toObject();
    QVERIFY(agents.contains(QStringLiteral("build")));

    const QJsonObject buildAgent = agents.value(QStringLiteral("build")).toObject();
    QCOMPARE(buildAgent.value(QStringLiteral("model")).toString(), spec.modelId);

    // Verify that a Trial was recorded under the storage root.
    const QString trialsPath = QDir(storageRoot).filePath(QStringLiteral("trials"));
    QDir trialsDir(trialsPath);
    QVERIFY(trialsDir.exists());
    const QStringList trialFiles = trialsDir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    QVERIFY(!trialFiles.isEmpty());

    // Verify that projects.json contains an association for this project.
    const QString projectsFilePath = QDir(storageRoot).filePath(QStringLiteral("projects.json"));
    QFile projectsFile(projectsFilePath);
    QVERIFY(projectsFile.exists());
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
        if (obj.value(QStringLiteral("path")).toString() == QDir::cleanPath(projectPath)) {
            found = true;
            QCOMPARE(obj.value(QStringLiteral("team_id")).toString(), team.id);
            QVERIFY(!obj.value(QStringLiteral("last_trial_id")).toString().isEmpty());
        }
    }
    QVERIFY(found);
}

QTEST_MAIN(TestApplyTeam)
#include "test_apply_team.moc"
