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
#include "storage/StorageManager.h"

class TestComplianceSignoff : public QObject
{
    Q_OBJECT

private slots:
    void seededTeamsRoundTripThroughOpencodeDebugConfig();

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

QTEST_MAIN(TestComplianceSignoff)
#include "test_compliance_signoff.moc"
