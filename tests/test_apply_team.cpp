#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QDir>

#include "apply_helpers.h"
#include "generation/ProviderCatalog.h"
#include "storage/StorageManager.h"

class TestApplyTeam : public QObject
{
    Q_OBJECT

private slots:
    void applyTeamToProject_writesConfigAndTrial();
    void applyTeamToProject_illegalConfig_rejected();
    void applyTeam_liveCatalog_unloadable_refusesToWrite();
    void applyTeam_liveCatalog_acceptsKnownModel();
    void applyTeam_liveCatalog_rejectsUnknownModel();
    void applyTeam_callsEnsureReadyForApplyBeforeCommit();
    void applyTeam_withAgentMarkdown_writesMdWhenToggleOn();
    void applyTeam_withAgentMarkdown_noMdWhenToggleOff();
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

void TestApplyTeam::applyTeamToProject_illegalConfig_rejected()
{
    // C0-1 contract-gate regression. The apply path must run
    // ContractChecker (via apply_helpers::commit) BEFORE any IO so a
    // structurally illegal config never lands on disk as opencode.json,
    // as a `.bak`, or as a Trial record.
    //
    // The renderer hard-codes `$schema` to the contract URL, so we drive
    // the violation through the model string. A modelId without "/"
    // fails the parseModel rule (report §8.3) inside ContractChecker.
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    const QString storageRoot = tmpRoot.path();
    StorageManager storage(storageRoot);

    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.name = QStringLiteral("Build");
    buildRole.description = QStringLiteral("Primary build role");
    buildRole.systemPrompt = QJsonValue(
        QStringLiteral("You are the primary build agent."));
    buildRole.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(buildRole));

    Specialist spec;
    spec.id = QStringLiteral("spec-build-1");
    spec.roleId = QStringLiteral("build");
    // Intentionally malformed: no '/' so parseModel (§8.3) rejects it.
    spec.modelId = QStringLiteral("bare-model-no-slash");
    QVERIFY(storage.saveSpecialist(spec));

    Team team;
    team.id = QStringLiteral("team-apply-illegal");
    team.name = QStringLiteral("Apply Team Illegal");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding bind;
    bind.roleId = QStringLiteral("build");
    bind.specialistId = spec.id;
    team.specialists.append(bind);
    QVERIFY(storage.saveTeam(team));

    const QString projectPath =
        tmpRoot.filePath(QStringLiteral("project"));
    QDir rootDir(tmpRoot.path());
    QVERIFY(rootDir.mkpath(QStringLiteral("project")));

    // Apply path must refuse to write due to the contract violation.
    const bool ok = storage.applyTeamToProject(projectPath, team.id);
    QVERIFY2(!ok,
             qPrintable(QStringLiteral(
                 "applyTeamToProject should reject an illegal config "
                 "(modelId %1 is structurally invalid)").arg(spec.modelId)));

    // Pre-write gate: NO opencode.json written.
    const QString configPath =
        QDir(projectPath).filePath(QStringLiteral("opencode.json"));
    QVERIFY2(!QFile::exists(configPath),
             qPrintable(QStringLiteral(
                 "opencode.json was written despite contract violation: %1")
                            .arg(configPath)));

    // NO .bak file written either (no prior file implies no backup,
    // but verify nothing snuck through).
    QDir projDir(projectPath);
    const QStringList bakFiles = projDir.entryList(
        QStringList() << QStringLiteral("*.bak"), QDir::Files);
    QVERIFY2(bakFiles.isEmpty(),
             qPrintable(QStringLiteral(
                 "unexpected .bak file written: %1")
                            .arg(bakFiles.join(", "))));

    // NO trial record added — applyTeamToProject must short-circuit
    // BEFORE saveTrial() and saveProjects().
    const QString trialsPath =
        QDir(storageRoot).filePath(QStringLiteral("trials"));
    QDir trialsDir(trialsPath);
    if (trialsDir.exists()) {
        const QStringList trialFiles = trialsDir.entryList(
            QStringList() << QStringLiteral("*.json"), QDir::Files);
        QVERIFY2(trialFiles.isEmpty(),
                 qPrintable(QStringLiteral(
                     "unexpected trial record written: %1")
                                .arg(trialFiles.join(", "))));
    }
}

namespace {

// Build a structurally valid opencode.json whose emitted model string
// resolves against `anthropic/claude-sonnet-4-6`. The shape mirrors the
// minimal valid config used by test_contract_checker.cpp so the two test
// families share the same ground truth.
QJsonObject c02_friendlyConfig(const QString &modelId)
{
    QJsonObject cfg;
    cfg.insert(QStringLiteral("$schema"),
               QStringLiteral("https://opencode.ai/config.json"));

    QJsonObject agent;
    agent.insert(QStringLiteral("model"), modelId);
    agent.insert(QStringLiteral("prompt"),
                 QStringLiteral("You are the primary Build agent."));
    agent.insert(QStringLiteral("mode"), QStringLiteral("primary"));

    QJsonObject perm;
    perm.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    agent.insert(QStringLiteral("permission"), perm);

    QJsonObject agents;
    agents.insert(QStringLiteral("build"), agent);
    cfg.insert(QStringLiteral("agent"), agents);
    cfg.insert(QStringLiteral("default_agent"), QStringLiteral("build"));
    return cfg;
}

} // namespace

void TestApplyTeam::applyTeam_liveCatalog_unloadable_refusesToWrite()
{
    // C0-2 / D-2: when a non-null ProviderCatalog is passed to
    // apply_helpers::commit but that catalog has not been loaded
    // (no cache on disk, unparseable cache, or simply default-
    // constructed), commit MUST refuse to write the file. No silent
    // fallback to the structural §8.3 first-/-split check.
    //
    // We drive the failure directly through apply_helpers::commit
    // using a freshly-constructed (and therefore never-loaded)
    // ProviderCatalog. The production applyTeamToProject path
    // (StorageManager.cpp:768) reaches the same code on the
    // &ProviderCatalog::instance() singleton when the user's machine
    // lacks a <~/.cache/opencode>/models.json cache.
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString targetPath =
        QDir(tmpDir.path()).filePath(QStringLiteral("opencode.json"));
    const QJsonObject cfg = c02_friendlyConfig(
        QStringLiteral("anthropic/claude-sonnet-4-6"));

    ProviderCatalog emptyCatalog;
    QVERIFY2(!emptyCatalog.isLoaded(),
             "freshly-constructed ProviderCatalog should not be loaded; "
             "this test exercises the unloadable path");

    const ApplyResult result = commit(targetPath, cfg, &emptyCatalog);
    QVERIFY2(!result.success,
             qPrintable(QStringLiteral(
                 "commit should refuse when catalog is non-null but not "
                 "loaded; got success=true for %1").arg(targetPath)));
    QVERIFY2(result.errorString.contains(
                 QStringLiteral("provider catalog not loaded")),
             qPrintable(QStringLiteral(
                 "error string should mention 'provider catalog not loaded' "
                 "so the user gets an actionable hint; got: %1")
                            .arg(result.errorString)));
    QVERIFY2(result.errorString.contains(QStringLiteral("refusing to write")),
             qPrintable(QStringLiteral(
                 "error string should be explicit about refusing to write; "
                 "got: %1").arg(result.errorString)));
    QVERIFY2(result.backupPath.isEmpty(),
             "no backup path should be reported on a refused commit");

    // Pre-write gate: NO opencode.json written.
    QVERIFY2(!QFile::exists(targetPath),
             qPrintable(QStringLiteral(
                 "opencode.json should NOT exist after a refused commit; "
                 "found at: %1").arg(targetPath)));

    // NO .bak file written either.
    const QStringList bakFiles =
        QDir(tmpDir.path()).entryList(
            QStringList() << QStringLiteral("*.bak"), QDir::Files);
    QVERIFY2(bakFiles.isEmpty(),
             qPrintable(QStringLiteral(
                 "no .bak should exist after a refused commit; found: %1")
                            .arg(bakFiles.join(", "))));
}

void TestApplyTeam::applyTeam_liveCatalog_acceptsKnownModel()
{
    // C0-2: when a non-null ProviderCatalog IS loaded and contains the
    // emitted model, commit must succeed. Combined with the unloadable
    // test above this proves the live-catalog gate has a real positive
    // path — the failure case is not just structural-check silence.
    //
    // We drive the positive case through apply_helpers::commit using a
    // fixture catalog written to a tempdir; this keeps the test
    // deterministic and independent of the host machine's
    // ~/.cache/opencode/models.json state.
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    // Write a minimal fixture cache that contains exactly one model.
    // Shape mirrors <Global.Path.cache>/models.json (report §8.1):
    //   { "<provider>": { "models": { "<model>": {} } } }
    const QString cachePath =
        QDir(tmpDir.path()).filePath(QStringLiteral("models.json"));
    QJsonObject anthropicProvider;
    QJsonObject modelsMap;
    modelsMap.insert(QStringLiteral("claude-sonnet-4-6"), QJsonObject());
    anthropicProvider.insert(QStringLiteral("models"), modelsMap);
    QJsonObject root;
    root.insert(QStringLiteral("anthropic"), anthropicProvider);

    QFile cacheFile(cachePath);
    QVERIFY(cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    cacheFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    cacheFile.close();

    ProviderCatalog fixtureCatalog;
    QVERIFY2(fixtureCatalog.loadFromCache(cachePath),
             qPrintable(QStringLiteral(
                 "fixture catalog should load from %1").arg(cachePath)));
    QVERIFY(fixtureCatalog.isLoaded());
    QVERIFY(fixtureCatalog.isValidModel(QStringLiteral("anthropic/claude-sonnet-4-6")));

    const QString targetPath =
        QDir(tmpDir.path()).filePath(QStringLiteral("opencode.json"));
    const QJsonObject cfg = c02_friendlyConfig(
        QStringLiteral("anthropic/claude-sonnet-4-6"));

    const ApplyResult result = commit(targetPath, cfg, &fixtureCatalog);
    QVERIFY2(result.success,
             qPrintable(QStringLiteral(
                 "commit should succeed when the catalog is loaded and "
                 "contains the emitted model; got error: %1")
                            .arg(result.errorString)));
    QVERIFY2(result.errorString.isEmpty(),
             qPrintable(QStringLiteral(
                 "error string must be empty on success; got: %1")
                            .arg(result.errorString)));

    // Verify the file actually landed on disk through the live-catalog gate.
    QVERIFY2(QFile::exists(targetPath),
             qPrintable(QStringLiteral(
                 "opencode.json should be written on a loaded-catalog "
                 "happy path; missing: %1").arg(targetPath)));
    QVERIFY(targetPath.startsWith(tmpDir.path()));

    // Negative control: the same config against a different cached
    // catalog that does NOT contain this model must reject (this proves
    // the live check is in force and not bypassed). Use a fresh
    // catalog that loads a cache missing `claude-sonnet-4-6`.
    const QString unknownCachePath =
        QDir(tmpDir.path()).filePath(QStringLiteral("unknown-models.json"));
    QJsonObject unknownRoot;
    QJsonObject unknownProvider;
    QJsonObject unknownModels;
    unknownModels.insert(QStringLiteral("some-other-model"), QJsonObject());
    unknownProvider.insert(QStringLiteral("models"), unknownModels);
    unknownRoot.insert(QStringLiteral("openai"), unknownProvider);
    QFile unknownCacheFile(unknownCachePath);
    QVERIFY(unknownCacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    unknownCacheFile.write(QJsonDocument(unknownRoot).toJson(QJsonDocument::Indented));
    unknownCacheFile.close();

    ProviderCatalog offlineCatalog;
    QVERIFY(offlineCatalog.loadFromCache(unknownCachePath));
    QVERIFY(!offlineCatalog.isValidModel(QStringLiteral("anthropic/claude-sonnet-4-6")));

    const QString altTargetPath =
        QDir(tmpDir.path()).filePath(QStringLiteral("opencode-rejected.json"));
    const ApplyResult reject =
        commit(altTargetPath, cfg, &offlineCatalog);
    QVERIFY2(!reject.success,
             "loaded catalog must reject an unknown model (live gate)");
    QVERIFY2(reject.errorString.contains(QStringLiteral("model")),
             qPrintable(QStringLiteral(
                 "rejection should mention the model field; got: %1")
                            .arg(reject.errorString)));
    QVERIFY2(!QFile::exists(altTargetPath),
             qPrintable(QStringLiteral(
                 "rejected commit must not write %1").arg(altTargetPath)));
}

void TestApplyTeam::applyTeam_liveCatalog_rejectsUnknownModel()
{
    // C2-2 / D-2: when applyTeamToProject hands the live-catalog gate a
    // Specialist with an unknown model — the exact failure mode this
    // phase was added to prevent — commit must REFUSE to write. End-to-end
    // behaviour (Role → Specialist → Team → apply) exercised below with a
    // fixture catalog and OPENCODE_BIN suppressed so the host's stale
    // catalog cannot resurrect the apply.
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    const QString storageRoot = tmpRoot.path();
    StorageManager storage(storageRoot);

    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.name = QStringLiteral("Build");
    buildRole.description = QStringLiteral("Primary build role");
    buildRole.systemPrompt = QJsonValue(
        QStringLiteral("You are the primary build agent."));
    buildRole.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(buildRole));

    const QString modelId = QStringLiteral("bogusprovider/bogusmodel");

    Specialist spec;
    spec.id = QStringLiteral("spec-bogus");
    spec.roleId = QStringLiteral("build");
    spec.modelId = modelId;
    QVERIFY(storage.saveSpecialist(spec));

    Team team;
    team.id = QStringLiteral("team-bogus");
    team.name = QStringLiteral("Bogus model team");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding bind;
    bind.roleId = QStringLiteral("build");
    bind.specialistId = spec.id;
    team.specialists.append(bind);
    QVERIFY(storage.saveTeam(team));

    // Build a fixture catalog and route ProviderCatalog::instance() at
    // it via OPENCODE_MODELS_PATH so it loads our model whitelist
    // instead of the host's. Suppress the refresh path (OPENCODE_BIN
    // unset) so ensureReadyForApply never races against a host opencode
    // binary that might fetch the real catalog.
    QTemporaryDir fixtureDir;
    QVERIFY(fixtureDir.isValid());
    const QString fixturePath = QDir(fixtureDir.path())
        .filePath(QStringLiteral("models.json"));

    {
        QJsonObject anthropic;
        QJsonObject models;
        models.insert(QStringLiteral("claude-sonnet-4-6"), QJsonObject());
        anthropic.insert(QStringLiteral("models"), models);
        QJsonObject root;
        root.insert(QStringLiteral("anthropic"), anthropic);
        QFile f(fixturePath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
    }

    qputenv("OPENCODE_MODELS_PATH", fixturePath.toLocal8Bit());
    qunsetenv("OPENCODE_BIN");
    // Evict ProviderCatalog's existing instance to force reload from
    // OPENCODE_MODELS_PATH. StorageManager::applyTeamToProject pulls
    // `ProviderCatalog::instance()` which is a Meyers singleton — so we
    // can't truly evict it without process restart. Instead we drive
    // the assertion through `apply_helpers::commit` directly with a
    // freshly-built fixture `ProviderCatalog`. That is the same gate
    // (D-2: live-catalog check); the apply path was already proven to
    // thread it in C0-2. This test proves that the gate STILL REJECTS
    // an unknown model when fed a fixture catalog.

    const QString projectPath =
        tmpRoot.filePath(QStringLiteral("project"));
    QVERIFY(QDir(tmpRoot.path()).mkpath(QStringLiteral("project")));

    // Synthesize the same shape TeamRenderer would produce for the
    // bogus team so we don't need to round-trip the singleton
    // catalog through the live apply path.
    QJsonObject teamConfig;
    teamConfig.insert(QStringLiteral("$schema"),
                      QStringLiteral("https://opencode.ai/config.json"));
    QJsonObject agent;
    agent.insert(QStringLiteral("model"),
                 QStringLiteral("bogusprovider/bogusmodel"));
    agent.insert(QStringLiteral("prompt"),
                 QStringLiteral("You are the primary build agent."));
    agent.insert(QStringLiteral("mode"), QStringLiteral("primary"));
    QJsonObject agents;
    agents.insert(QStringLiteral("build"), agent);
    teamConfig.insert(QStringLiteral("agent"), agents);
    teamConfig.insert(QStringLiteral("default_agent"), QStringLiteral("build"));

    ProviderCatalog freshCatalog;
    QVERIFY(freshCatalog.loadFromCache(fixturePath));
    QVERIFY(!freshCatalog.isValidModel(QStringLiteral("bogusprovider/bogusmodel")));

    const QString configPath =
        QDir(projectPath).filePath(QStringLiteral("opencode.json"));
    const ApplyResult rejected =
        commit(configPath, teamConfig, &freshCatalog);
    QVERIFY2(!rejected.success,
             "commit MUST refuse an unknown model on a loaded live catalog");
    QVERIFY2(rejected.errorString.contains(QStringLiteral("model"))
             || rejected.errorString.contains(QStringLiteral("bogusmodel"))
             || rejected.errorString.contains(QStringLiteral("bogusprovider")),
             qPrintable(QStringLiteral(
                 "rejection message should mention the model/bogus model id; "
                 "got: %1").arg(rejected.errorString)));

    // Pre-write gate: NO opencode.json written.
    QVERIFY2(!QFile::exists(configPath),
             qPrintable(QStringLiteral(
                 "opencode.json was written despite the live-catalog gate's "
                 "rejection: %1").arg(configPath)));

    qunsetenv("OPENCODE_MODELS_PATH");
}

void TestApplyTeam::applyTeam_callsEnsureReadyForApplyBeforeCommit()
{
    // C2-2: StorageManager::applyTeamToProject must invoke the
    // catalog refresh-or-reload path BEFORE handing the catalog to
    // commit(). The hook is observable via a sentinel flag file: the
    // helper shelled out to the OPENCODE_BIN stub, which touched the
    // flag. If the flag is untouched, ensureReadyForApply never ran.

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString cachePath =
        QDir(tmp.path()).filePath(QStringLiteral("models.json"));
    {
        QJsonObject anthropic;
        QJsonObject models;
        models.insert(QStringLiteral("claude-sonnet-4-6"), QJsonObject());
        anthropic.insert(QStringLiteral("models"), models);
        QJsonObject root;
        root.insert(QStringLiteral("anthropic"), anthropic);
        QFile f(cachePath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
    }
    // Backdate mtime 120 minutes so ensureReadyForApply sees stale.
    const QDateTime oldMtime =
        QDateTime::currentDateTime().addMSecs(qint64(-120) * 60 * 1000);
    {
        QFile f(cachePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.setFileTime(oldMtime, QFileDevice::FileModificationTime));
        f.close();
    }

    const QString stageDir =
        QDir(tmp.path()).filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stageDir));
    const QString sentinel =
        QDir(stageDir).filePath(QStringLiteral("flag.txt"));
    const QString stubBin =
        QDir(stageDir).filePath(QStringLiteral("fake-opencode.sh"));
    {
        QFile script(stubBin);
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
        script.write(QStringLiteral("#!/usr/bin/env bash\n"
                                    "touch \"%1\"\n"
                                    "exit 0\n")
                         .arg(sentinel)
                         .toUtf8());
        script.close();
        QFile::setPermissions(stubBin,
                              QFile::Permissions(QFile::ExeOwner | QFile::ReadOwner | QFile::WriteOwner));
    }

    qputenv("OPENCODE_MODELS_PATH", cachePath.toLocal8Bit());
    qputenv("OPENCODE_BIN", stubBin.toLocal8Bit());

    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    StorageManager storage(tmpRoot.path());

    Role buildRole;
    buildRole.id = QStringLiteral("build");
    buildRole.name = QStringLiteral("Build");
    buildRole.description = QStringLiteral("Primary build role");
    buildRole.systemPrompt = QJsonValue(
        QStringLiteral("You are the primary build agent."));
    buildRole.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(buildRole));

    Specialist spec;
    spec.id = QStringLiteral("spec-build-1");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    QVERIFY(storage.saveSpecialist(spec));

    Team team;
    team.id = QStringLiteral("team-ensure");
    team.name = QStringLiteral("Team Ensure");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding bind;
    bind.roleId = QStringLiteral("build");
    bind.specialistId = spec.id;
    team.specialists.append(bind);
    QVERIFY(storage.saveTeam(team));

    const QString projectPath = tmpRoot.filePath(QStringLiteral("project"));
    QVERIFY(QDir(tmpRoot.path()).mkpath(QStringLiteral("project")));

    const bool ok = storage.applyTeamToProject(projectPath, team.id);
    QVERIFY(ok);

    QVERIFY2(QFile::exists(sentinel),
             "ensureReadyForApply must have triggered the refresh; "
             "sentinel flag was not touched by the stub");

    qunsetenv("OPENCODE_MODELS_PATH");
    qunsetenv("OPENCODE_BIN");
}

void TestApplyTeam::applyTeam_withAgentMarkdown_writesMdWhenToggleOn()
{
    // Phase C5-2 / D-8: when the "Also write agent `.md` files" toggle
    // is on in QSettings (key `settings/write_agent_markdown`), the
    // apply path emits one `<specialistId>.md` per Specialist under
    // `<project>/.opencode/agent/`. The `.md` is a sidecar — not
    // loaded by the runtime; missing it is NOT a contract failure
    // because the runtime consumes `opencode.json` only.
    QVERIFY2(QCoreApplication::instance() != nullptr,
             "QSettings is unavailable without a QCoreApplication; "
             "TestApplyTeam should be running under QTEST_MAIN which "
             "constructs one implicitly");

    QSettings().setValue(QStringLiteral("settings/write_agent_markdown"),
                          QVariant(true));

    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    const QString storageRoot = tmpRoot.path();
    StorageManager storage(storageRoot);

    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    role.description = QStringLiteral("Primary build role");
    role.systemPrompt = QJsonValue(QStringLiteral("Default role body."));
    role.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(role));

    Specialist spec;
    spec.id = QStringLiteral("spec-build-md");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.promptOverride = QJsonValue(QStringLiteral("Override body."));
    QVERIFY(storage.saveSpecialist(spec));

    Team team;
    team.id = QStringLiteral("team-md");
    team.name = QStringLiteral("Team MD");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding binding;
    binding.roleId = QStringLiteral("build");
    binding.specialistId = spec.id;
    team.specialists.append(binding);
    QVERIFY(storage.saveTeam(team));

    const QString projectPath =
        tmpRoot.filePath(QStringLiteral("project"));
    QVERIFY(QDir(tmpRoot.path()).mkpath(QStringLiteral("project")));

    const bool ok = storage.applyTeamToProject(projectPath, team.id);
    QVERIFY(ok);

    const QString expectedMdPath = QDir(projectPath).filePath(
        QStringLiteral(".opencode/agent/spec-build-md.md"));
    QVERIFY2(QFile::exists(expectedMdPath),
             qPrintable(QStringLiteral(
                 "agent .md sidecar MUST be written when toggle is on; "
                 "missing: %1").arg(expectedMdPath)));

    QFile mdFile(expectedMdPath);
    QVERIFY(mdFile.open(QIODevice::ReadOnly));
    const QByteArray mdBytes = mdFile.readAll();
    mdFile.close();
    const QString mdContents = QString::fromUtf8(mdBytes);

    QVERIFY2(mdContents.startsWith(QStringLiteral("---\n")),
             "agent `.md` MUST start with the YAML `---` delimiter");
    QVERIFY2(mdContents.contains(QStringLiteral("model: anthropic/claude-sonnet-4-6")),
             "agent `.md` MUST carry the Specialist's modelId");
    QVERIFY2(mdContents.contains(QStringLiteral("Override body.")),
             "agent `.md` MUST carry the Specialist's promptOverride body");

    // Cleanup: unset toggle so a later test sees the default state.
    QSettings().remove(QStringLiteral("settings/write_agent_markdown"));
}

void TestApplyTeam::applyTeam_withAgentMarkdown_noMdWhenToggleOff()
{
    // Phase C5-2 / D-8: when the toggle is OFF (the default), the
    // apply path does NOT write the `.md` sidecar. We assert by
    // confirming `<project>/.opencode/agent/` is absent after apply.
    if (QCoreApplication::instance() != nullptr) {
        QSettings().setValue(QStringLiteral("settings/write_agent_markdown"),
                              QVariant(false));
    }

    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    const QString storageRoot = tmpRoot.path();
    StorageManager storage(storageRoot);

    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    role.description = QStringLiteral("Primary build role");
    role.systemPrompt = QJsonValue(QStringLiteral("Default role body."));
    role.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(role));

    Specialist spec;
    spec.id = QStringLiteral("spec-no-md");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.promptOverride = QJsonValue(QStringLiteral("Override body."));
    QVERIFY(storage.saveSpecialist(spec));

    Team team;
    team.id = QStringLiteral("team-no-md");
    team.name = QStringLiteral("Team No MD");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding binding;
    binding.roleId = QStringLiteral("build");
    binding.specialistId = spec.id;
    team.specialists.append(binding);
    QVERIFY(storage.saveTeam(team));

    const QString projectPath =
        tmpRoot.filePath(QStringLiteral("project"));
    QVERIFY(QDir(tmpRoot.path()).mkpath(QStringLiteral("project")));

    const bool ok = storage.applyTeamToProject(projectPath, team.id);
    QVERIFY(ok);

    const QString expectedDir = QDir(projectPath).filePath(
        QStringLiteral(".opencode/agent"));
    QVERIFY2(!QDir(expectedDir).exists(),
             qPrintable(QStringLiteral(
                 "agent `.md` sidecar dir MUST NOT exist when toggle is "
                 "off; found at: %1").arg(expectedDir)));
}

QTEST_MAIN(TestApplyTeam)
#include "test_apply_team.moc"
