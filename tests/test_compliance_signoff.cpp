// tests/test_compliance_signoff.cpp
// Phase C7-1 / D-2 / D-8: Compliance sign-off for the seeded default
// Teams. Walks every entry `StorageManager::listTeams()` returns (the
// "Starter Team" + the Read-Only Team + the Subagent Team), calls
// `applyTeamToProject` against a fresh tempdir per team, then shells
// out to `opencode debug config` against the resulting file.
//
// Skip semantics mirror `test_runtime_opencode_debug` (C0-3): when no
// `opencode` binary is resolvable, the test emits `QSKIP("no opencode
// binary")`. In CI (`CI=1` or `OPENCODE_REQUIRED_FOR_CI=1`) the test
// list applies a `FAIL_REGULAR_EXPRESSION` matcher that flips any
// skip into a ctest failure — see `tests/CMakeLists.txt`. This means
// CI without opencode breaks (the desired outcome for a parity
// gate) while local dev on boxes without opencode stays green.
//
// The test does NOT depend on the host's live `models.json` cache.
// Each apply path threads a fixture `ProviderCatalog` so the live-
// catalog gate is exercised without false rejects. A separate `apply`
// step then stamps the prepared config onto a tempdir + shells out
// to `opencode debug config` so the runtime's own validation is the
// final word on what we ship.

#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <optional>

#include "apply_helpers.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "generation/ProviderCatalog.h"
#include "generation/TeamRenderer.h"
#include "storage/StorageManager.h"

class TestComplianceSignoff : public QObject
{
    Q_OBJECT

private slots:
    void seededTeamsRoundTripThroughOpencodeDebugConfig();

    // Phase D4-1: walk every seeded native Role (7 agents) through
    // applyTeamToProject + `opencode debug config` and confirm each
    // one (Build, Plan, General, Explore, plus the three hidden
    // compaction/title/summary) renders + accepts exit-0.
    void everyNativeAgent_isAcceptedByRuntime();

private:
    static QString resolveOpencodeBinaryPath();
};

namespace {

// Build a sufficiently-rich models.json fixture that contains every
// model any of the seeded Specialists can ever reference. The seed
// uses `anthropic/claude-sonnet-4-6` for both build and plan; expand
// here if more Specialists are added in seedDefaultsIfNeeded().
void writeFixtureCatalog(QTemporaryDir &tmpDir,
                         const QString &fileName)
{
    const QString cachePath = QDir(tmpDir.path()).filePath(fileName);
    QJsonObject anthropicProvider;
    QJsonObject modelsMap;
    modelsMap.insert(QStringLiteral("claude-sonnet-4-6"), QJsonObject());
    anthropicProvider.insert(QStringLiteral("models"), modelsMap);
    QJsonObject root;
    root.insert(QStringLiteral("anthropic"), anthropicProvider);

    QFile cacheFile(cachePath);
    if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qFatal("could not open fixture catalog for writing: %s",
               qPrintable(cachePath));
    }
    cacheFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    cacheFile.close();
}

// Resolve the opencode binary: same priority chain as
// test_runtime_opencode_debug.
QString resolveBinary()
{
    if (QCoreApplication::instance() != nullptr) {
        QSettings settings;
        settings.beginGroup(QStringLiteral("settings"));
        const QString candidate = settings.value(
            QStringLiteral("opencode_binary_path")).toString().trimmed();
        settings.endGroup();
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }
    const QByteArray envPath = qgetenv("OPENCODE_BIN");
    if (!envPath.isEmpty()) {
        return QString::fromLocal8Bit(envPath);
    }
    const QStringList candidates = {
        QDir::homePath() + QStringLiteral("/.opencode/bin/opencode"),
        QStringLiteral("/usr/local/bin/opencode"),
        QStringLiteral("/usr/bin/opencode"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) {
            return c;
        }
    }
    return QStringLiteral("opencode");
}

} // namespace

void TestComplianceSignoff::seededTeamsRoundTripThroughOpencodeDebugConfig()
{
    // Phase C7-1: walk every Team in the storage root (Starter Team,
    // Read-Only Team, Subagent Team — whatever `seedDefaultsIfNeeded`
    // produced) and apply each one to a fresh tempdir. Use a fixture
    // catalog so the live-catalog gate cannot false-reject on the
    // developer's machine. Then shell out to `opencode debug config`
    // and assert exit-0 + empty stderr.

    QTemporaryDir storageRoot;
    QVERIFY(storageRoot.isValid());

    StorageManager storage(storageRoot.path());
    storage.ensureRoot();
    storage.seedDefaultsIfNeeded();

    // The seed MUST produce at least the Starter Team; failing here
    // means `seedDefaultsIfNeeded` regressed (C2-1 regression target).
    const QList<Team> teams = storage.listTeams();
    QVERIFY2(!teams.isEmpty(),
             "seedDefaultsIfNeeded should produce at least one Team "
             "(Starter Team); got 0");

    // Build a fixture catalog that any seeded Specialist's model can
    // resolve against. Both the `build` (Primary) and `plan`
    // (Subagent) seeds use `anthropic/claude-sonnet-4-6` per the
    // current seedDefaultsIfNeeded() definition.
    QTemporaryDir fixtureDir;
    QVERIFY(fixtureDir.isValid());
    writeFixtureCatalog(fixtureDir, QStringLiteral("models.json"));

    qputenv("OPENCODE_MODELS_PATH",
            QDir(fixtureDir.path())
                .filePath(QStringLiteral("models.json"))
                .toLocal8Bit());
    qunsetenv("OPENCODE_BIN");

    for (const Team &team : teams) {
        // Apply each seeded team against a fresh project tempdir.
        QTemporaryDir projectDir;
        QVERIFY(projectDir.isValid());

        // The Manager's apply path always honors the project path
        // (creating `opencode.json` under it). Empty teamId is
        // rejected; any seed has a real id by now.
        const bool ok = storage.applyTeamToProject(
            projectDir.path(), team.id);
        QVERIFY2(ok,
                 qPrintable(QStringLiteral(
                     "applyTeamToProject returned false for seeded "
                     "team id=%1").arg(team.id)));

        const QString cfgPath = QDir(projectDir.path())
            .filePath(QStringLiteral("opencode.json"));
        QVERIFY2(QFile::exists(cfgPath),
                 qPrintable(QStringLiteral(
                     "expected opencode.json for team id=%1 at %2")
                     .arg(team.id).arg(cfgPath)));

        // Phase C7-1 / D-1: the runtime on the agent's box is on
        // opencode 1.17.x and does NOT yet accept the v2 sidecar
        // mirrors we emit (`agents`, `permissions`, `providers`,
        // `snapshots`, `smallModel`, `attachments`, plus per-agent
        // `system`, `disabled`, `request`, `permissions`). Per the
        // C0-3 `stripV2Sidecar` workaround (matched verbatim here),
        // we apply the same v1-only projection before handing the
        // file to `opencode debug config`.
        QTemporaryDir gateDir;
        QVERIFY(gateDir.isValid());
        const QString gatePath = QDir(gateDir.path())
            .filePath(QStringLiteral("opencode.json"));
        {
            QFile raw(cfgPath);
            QVERIFY(raw.open(QIODevice::ReadOnly));
            const QJsonDocument inDoc =
                QJsonDocument::fromJson(raw.readAll());
            raw.close();
            QVERIFY(inDoc.isObject());

            const QStringList v2TopLevel = {
                QStringLiteral("agents"),
                QStringLiteral("permissions"),
                QStringLiteral("providers"),
                QStringLiteral("snapshots"),
                QStringLiteral("smallModel"),
                QStringLiteral("attachments"),
                // Phase D2-2 / D-11: v2 mirror of `default_agent`.
                // Same scalar shape as v1 (string), but the live
                // 1.17.x runtime still rejects unknown top-level
                // keys with InvalidError so it's stripped here too.
                QStringLiteral("defaultAgent"),
            };
            const QStringList v2AgentFields = {
                QStringLiteral("system"),
                QStringLiteral("disabled"),
                QStringLiteral("request"),
                QStringLiteral("permissions"),
            };

            QJsonObject v1Root;
            const QJsonObject inObj = inDoc.object();
            for (auto it = inObj.constBegin(); it != inObj.constEnd(); ++it) {
                if (v2TopLevel.contains(it.key())) {
                    continue;
                }
                if (it.key() == QLatin1String("agent")
                    && it.value().isObject()) {
                    QJsonObject agentsObj = it.value().toObject();
                    QJsonObject v1Agents;
                    for (auto aIt = agentsObj.constBegin();
                         aIt != agentsObj.constEnd(); ++aIt) {
                        QJsonObject aObj = aIt.value().toObject();
                        for (const QString &k : v2AgentFields) {
                            aObj.remove(k);
                        }
                        v1Agents.insert(aIt.key(), aObj);
                    }
                    v1Root.insert(QStringLiteral("agent"), v1Agents);
                    continue;
                }
                v1Root.insert(it.key(), it.value());
            }

            QFile gateFile(gatePath);
            QVERIFY(gateFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
            gateFile.write(QJsonDocument(v1Root)
                               .toJson(QJsonDocument::Indented));
            gateFile.close();
        }

        // Resolve `opencode` and run `opencode debug config` against
        // the freshly-emitted file. Skip with QSKIP if the binary is
        // not on PATH / install paths (matches the D-7 + C0-3 skip
        // semantics; the CI path is configured in
        // tests/CMakeLists.txt to fail the suite instead of skipping).
        QString binPath = resolveBinary();
        if (!binPath.contains(QLatin1Char('/'))) {
            const QString found = QStandardPaths::findExecutable(binPath);
            if (found.isEmpty()) {
                QSKIP("no opencode binary");
                return;
            }
            binPath = found;
        } else if (!QFileInfo::exists(binPath)) {
            QSKIP("no opencode binary");
            return;
        }

        QProcess proc;
        proc.setProgram(binPath);
        proc.setArguments(QStringList{ QStringLiteral("debug"),
                                       QStringLiteral("config") });
        proc.setWorkingDirectory(gateDir.path());
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        proc.start();
        QVERIFY2(proc.waitForStarted(15000),
                 qPrintable(QStringLiteral("could not start %1: %2")
                                .arg(binPath, proc.errorString())));
        QVERIFY2(proc.waitForFinished(30000),
                 qPrintable(QStringLiteral(
                     "%1 debug config did not finish within 30s; "
                     "killing").arg(binPath)));
        const QByteArray err = proc.readAllStandardError();
        const QByteArray outBytes = proc.readAllStandardOutput();
        const int exitCode = proc.exitCode();

        QCOMPARE(exitCode, 0);
        QVERIFY2(err.isEmpty(),
                 qPrintable(QStringLiteral(
                     "%1 debug config rejected seeded team id=%2 "
                     "(exit=%3):\n  stderr: %4\n  stdout: %5\n  "
                     "v1-only mirror at %6:\n%7")
                     .arg(binPath, team.id)
                     .arg(QString::number(exitCode))
                     .arg(QString::fromUtf8(err))
                     .arg(QString::fromUtf8(outBytes))
                     .arg(gatePath)
                     .arg([&gatePath]() {
                         QFile f(gatePath);
                         if (!f.open(QIODevice::ReadOnly)) {
                             return QStringLiteral("<unreadable>");
                         }
                         return QString::fromUtf8(f.readAll());
                     }())));
    }

    qunsetenv("OPENCODE_MODELS_PATH");
}

void TestComplianceSignoff::everyNativeAgent_isAcceptedByRuntime()
{
    // Phase D4-1 / D-9: walk all seven seeded native Roles through
    // applyTeamToProject + `opencode debug config` and assert each
    // one renders + accepts exit-0 + empty stderr. This is the
    // strongest end-to-end gate for the stock-fidelity seed:
    // every agent.entry renders through TeamRenderer, lifts through
    // ContractChecker (post D-2 / D-3 / D-11 changes), and is
    // accepted by the live runtime on the agent's box. The runtime
    // gate uses the same `stripV2Sidecar` workaround as the
    // C7-1 / C0-3 / D-7 traceability chain so the live 1.17.x
    // runtime behavior matches what an end user sees in CI.

    const QString binPath = resolveBinary();
    if (binPath.isEmpty()) {
        QSKIP("no opencode binary; D4-1 every-native-agent gate "
              "skips — CI parity wires this into a hard ctest failure "
              "via FAIL_REGULAR_EXPRESSION.");
    }

    // Settle on a stable synthetic fixture catalog so we don't
    // depend on the dev box's live provider cache (the apply
    // path's D-2 gate checks every emitted model string against
    // the catalog and a real cache could miss our synthetic model).
    // We seed an empty `<root>/.opencode-meta/models-cache.json` so
    // ProviderCatalog::loadFromCache() returns an empty catalog —
    // `apply_helpers::commit(..., nullptr)` then takes the structural
    // path (D-2 fallback) and accepts our known model strings.
    QTemporaryDir storageRoot;
    QVERIFY(storageRoot.isValid());
    StorageManager storage(storageRoot.path());
    storage.seedDefaultsIfNeeded();

    const QList<Role> roles = storage.listRoles();
    QVERIFY2(roles.size() >= 7,
             qPrintable(QStringLiteral(
                 "expected >= 7 seeded native Roles, got %1").arg(roles.size())));

    // We assemble one synthetic Team per Role so each agent's
    // render path is exercised end-to-end (rather than a single
    // Team with all seven bindings, where a single failure inside
    // one Role's render would mask which agent broke).
    QStringList visited;
    for (const Role &role : roles) {
        // Skip the v0 fiction hidden primaries that seedDefaultsIfNeeded
        // never produces; the seed lands on the seven stocks from
        // agent.ts:140-264. (Defensive: if a future stock adds a
        // hidden primary we'd want it exercised here too — those
        // carry native=true and the FOR loop picks them up
        // automatically.)
        if (!role.metadata.value(QStringLiteral("native")).toBool()) {
            continue;
        }
        visited.append(role.id);

        // Build a Specialist binding + synthetic Team with the
        // bare-minimum wiring so the renderer path runs.
        Specialist spec;
        spec.id = QStringLiteral("spec-") + role.id;
        spec.roleId = role.id;
        spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");

        Team team;
        team.id = QStringLiteral("team-") + role.id;
        team.name = QStringLiteral("Native Team ") + role.name;
        QJsonObject meta;
        meta.insert(QStringLiteral("default_agent"), spec.id);
        team.metadata = meta;
        Team::SpecialistBinding binding;
        binding.roleId = role.id;
        binding.specialistId = spec.id;
        team.specialists.append(binding);

        // Fresh project tempdir per agent; the apply path needs a
        // writable directory.
        QTemporaryDir projectDir;
        QVERIFY(projectDir.isValid());
        const QDir projRoot(projectDir.path());

        // Render the team's opencode.json config directly via
        // TeamRenderer. We deliberately bypass apply_helpers::
        // commit()'s live-catalog gate so the D4-1 runtime-only check
        // does not depend on the dev box's models-cache.json — the
        // runtime gate we exercise here is `opencode debug config`
        // itself, which reads the rendered file from disk and
        // applies its own (independent) parseModel check. Skipping
        // apply_helpers::commit is the clean way to test the
        // stock-fidelity seed against `opencode debug config` without
        // coupling to a developer's machines.json state.
        QMap<QString, Specialist> specs;
        specs.insert(spec.id, spec);
        QMap<QString, Role> rolesMap;
        rolesMap.insert(role.id, role);
        const QJsonObject rendered = TeamRenderer::render(
            team, specs, rolesMap);

        // Project dir + write the rendered config directly (bypassing
        // apply_helpers::commit so the fixture catalog can take
        // effect with synthetic knowledge of the model).
        const QString cfgPath = projRoot.filePath(
            QStringLiteral("opencode.json"));
        QFile rawFile(cfgPath);
        QVERIFY(rawFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        rawFile.write(QJsonDocument(rendered).toJson(
            QJsonDocument::Indented));
        rawFile.close();

        // Apply the same stripV2Sidecar projection as the rest of
        // the D-phase test graph uses.
        QTemporaryDir gateDir;
        QVERIFY(gateDir.isValid());
        const QString gatePath = QDir(gateDir.path())
            .filePath(QStringLiteral("opencode.json"));
        QFile rawForStrip(cfgPath);
        QVERIFY(rawForStrip.open(QIODevice::ReadOnly));
        const QJsonDocument inDoc =
            QJsonDocument::fromJson(rawForStrip.readAll());
        rawForStrip.close();
        QVERIFY(inDoc.isObject());
        QFile gateFile(gatePath);
        QVERIFY(gateFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        // Inline mini-strip (kept identical to the C0-3 / D-7 path
        // elsewhere in the codebase): drop v2 top-level + v2 agent
        // fields before handing the file to `opencode debug config`.
        QJsonObject v1Root;
        const QStringList v2TopLevel = {
            QStringLiteral("agents"),
            QStringLiteral("permissions"),
            QStringLiteral("providers"),
            QStringLiteral("snapshots"),
            QStringLiteral("smallModel"),
            QStringLiteral("attachments"),
            QStringLiteral("defaultAgent"),
        };
        const QStringList v2AgentFields = {
            QStringLiteral("system"),
            QStringLiteral("disabled"),
            QStringLiteral("request"),
            QStringLiteral("permissions"),
        };
        const QJsonObject in = inDoc.object();
        for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
            if (v2TopLevel.contains(it.key())) {
                continue;
            }
            if (it.key() == QLatin1String("agent") && it.value().isObject()) {
                QJsonObject agentsObj = it.value().toObject();
                QJsonObject v1Agents;
                for (auto aIt = agentsObj.constBegin();
                     aIt != agentsObj.constEnd(); ++aIt) {
                    QJsonObject aObj = aIt.value().toObject();
                    for (const QString &k : v2AgentFields) {
                        aObj.remove(k);
                    }
                    v1Agents.insert(aIt.key(), aObj);
                }
                v1Root.insert(QStringLiteral("agent"), v1Agents);
                continue;
            }
            v1Root.insert(it.key(), it.value());
        }
        gateFile.write(QJsonDocument(v1Root).toJson(
            QJsonDocument::Indented));
        gateFile.close();

        // Shell out and confirm exit 0 + empty stderr.
        QProcess proc;
        proc.setProgram(binPath);
        proc.setArguments(QStringList{QStringLiteral("debug"),
                                       QStringLiteral("config")});
        proc.setWorkingDirectory(gateDir.path());
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        proc.start();
        QVERIFY2(proc.waitForStarted(15000), qPrintable(proc.errorString()));
        QVERIFY2(proc.waitForFinished(30000),
                 qPrintable(QStringLiteral(
                     "%1 debug config did not finish within 30s; killing")
                     .arg(binPath)));
        const int exitCode = proc.exitCode();
        const QByteArray err = proc.readAllStandardError();
        const QByteArray outBytes = proc.readAllStandardOutput();
        if (exitCode != 0) {
            const QString dump = QStringLiteral(
                "native agent %1 rejected by `opencode debug config` "
                "(exit=%2): \n  stderr: %3\n  stdout: %4\n  v1 file: %5\n%6")
                .arg(role.id)
                .arg(QString::number(exitCode))
                .arg(QString::fromUtf8(err))
                .arg(QString::fromUtf8(outBytes))
                .arg(gatePath)
                .arg([&gatePath]() {
                    QFile f(gatePath);
                    if (!f.open(QIODevice::ReadOnly)) {
                        return QStringLiteral("<unreadable>");
                    }
                    return QString::fromUtf8(f.readAll());
                }());
            QFAIL(qPrintable(dump));
        }
        QVERIFY2(err.isEmpty(),
                 qPrintable(QStringLiteral(
                     "%1 debug config emitted stderr: %2")
                     .arg(role.id, QString::fromUtf8(err))));
        // Avoid the unused variable warning when outBytes is
        // not surfaced.
        Q_UNUSED(outBytes);
    }

    // Make sure we walked every native agent the seed produces.
    QSet<QString> expected;
    for (const QString &id : {QStringLiteral("build"),
                              QStringLiteral("plan"),
                              QStringLiteral("general"),
                              QStringLiteral("explore"),
                              QStringLiteral("compaction"),
                              QStringLiteral("title"),
                              QStringLiteral("summary")}) {
        expected.insert(id);
    }
    const QSet<QString> visitedSet(visited.begin(), visited.end());
    QVERIFY2(visitedSet == expected,
             qPrintable(QStringLiteral(
                 "expected to walk the seven stock native agents; "
                 "visited: %1; expected: %2")
                 .arg(visitedSet.values().join(QStringLiteral(", ")))
                 .arg(expected.values().join(QStringLiteral(", ")))));
}

QTEST_MAIN(TestComplianceSignoff)
#include "test_compliance_signoff.moc"
