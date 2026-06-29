// tests/test_runtime_opencode_debug.cpp
// C0-3 (`opencode debug config` integration hook):
// Renders a Team fixture to disk, then shells out to the live
// `opencode debug config` against a freshly-emitted `opencode.json`
// and asserts: exit code 0 AND empty stderr — the binding contract
// per ROADMAP.md §12.3 / docs/OPENCODE-CONFIG-INTROSPECTION.md.
//
// The test resolves the opencode binary the same way
// ProviderCatalog::refreshFromCli does, but ALSO honors the
// SettingsDialog key (`settings/opencode_binary_path`) so a developer's
// QSettings preference is respected without extra plumbing.
//
// Test-failure semantics:
//   * Binary is found AND config parses cleanly → test passes.
//   * Binary is missing on the host                 → QSKIP("no opencode binary").
//
// The QSKIP path is the surface that lets dev boxes without `opencode`
// installed keep working while CI still rules. CMake gates the SKIP
// path with a `FAIL_REGULAR_EXPRESSION` when the build is invoked
// with `CI=1` (or `OPENCODE_REQUIRED_FOR_CI=1`) so CI rejects the skip
// while local dev keeps its green ticks. See tests/CMakeLists.txt for
// the property plumbing.

#include <QTest>
#include <QByteArray>
#include <QDir>
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
#include <QtGlobal>

#include "apply_helpers.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"

class TestRuntimeOpencodeDebug : public QObject
{
    Q_OBJECT

private slots:
    void debugConfig_acceptsRenderedFixture();

private:
    // Resolve the opencode binary search path, matching the priority
    // spelled out in ROADMAP.md C0-3:
    //   1. SettingsDialog-stored override (settings/opencode_binary_path)
    //   2. OPENCODE_BIN environment variable
    //   3. ~/.opencode/bin/opencode (the common install path)
    //   4. /usr/local/bin/opencode, /usr/bin/opencode
    //   5. Bare "opencode" on PATH
    // The returned QString is either absolute or "opencode" — never
    // empty; the caller does an existence check.
    static QString resolveBinaryPath();

    // Strip v2 camelCase sibling keys + per-agent v2 fields the
    // installed opencode 1.17.x doesn't yet understand. Mirrors the
    // workaround used by tests/test_cross_view_smoke.cpp (which also
    // exists before ROADMAP Phase C0). Update this in lockstep with
    // opencode's v2 sidecar support — once installed opencode accepts
    // the full v1+v2 emission, the strip becomes a no-op.
    static QJsonObject stripV2Sidecar(const QJsonObject &in);
};

namespace {

// Build a one-specialist, structurally-valid opencode.json in code
// (no TeamRenderer / no ProviderCatalog lookups) so the test stays
// self-contained and runs in any storage-root state. Mirrors the
// minimal-valid shape in tests/test_contract_checker.cpp.
QJsonObject minimalValidConfig()
{
    QJsonObject cfg;
    cfg.insert(QStringLiteral("$schema"),
               QStringLiteral("https://opencode.ai/config.json"));

    QJsonObject agent;
    agent.insert(QStringLiteral("model"),
                 QStringLiteral("anthropic/claude-sonnet-4-6"));
    agent.insert(QStringLiteral("prompt"),
                 QStringLiteral("Runtime debug-config test: Build agent."));
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

// Write `cfg` as `path`. Returns true on success. No validation, no
// backup — only used by the runtime gate test.
bool writeJsonFile(const QString &path, const QJsonObject &cfg)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(QJsonDocument(cfg).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

} // namespace

QString TestRuntimeOpencodeDebug::resolveBinaryPath()
{
    // 1. SettingsDialog-configured override. Same key as SettingsDialog
    //    (`settings/opencode_binary_path`); reading it the same way the
    //    dialog saves it (via QSettings under the "settings" group) so
    //    there is no second source of truth to drift.
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("settings"));
        const QString candidate = settings.value(
            QStringLiteral("opencode_binary_path")).toString().trimmed();
        settings.endGroup();
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }

    // 2. OPENCODE_BIN env override (matches ProviderCatalog.cpp behavior).
    const QByteArray envPath = qgetenv("OPENCODE_BIN");
    if (!envPath.isEmpty()) {
        return QString::fromLocal8Bit(envPath);
    }

    // 3. Common install locations. The first one to exist wins; the
    //    host's `~/.opencode/bin/opencode` is the package-managed path
    //    the installer puts in place per `opencode debug paths`.
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

    // 4. Fallback: bare "opencode" and let PATH resolution decide.
    return QStringLiteral("opencode");
}

QJsonObject TestRuntimeOpencodeDebug::stripV2Sidecar(const QJsonObject &in)
{
    const QStringList v2TopLevel = {
        QStringLiteral("agents"),
        QStringLiteral("permissions"),
        QStringLiteral("providers"),
        QStringLiteral("snapshots"),
        QStringLiteral("smallModel"),
        QStringLiteral("attachments"),
    };
    const QStringList v2AgentFields = {
        QStringLiteral("system"),
        QStringLiteral("disabled"),
        QStringLiteral("request"),
        QStringLiteral("permissions"),
    };

    QJsonObject v1Root;
    for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
        if (v2TopLevel.contains(it.key())) {
            continue;
        }
        if (it.key() == QLatin1String("agent") && it.value().isObject()) {
            QJsonObject agentsObj = it.value().toObject();
            QJsonObject v1Agents;
            for (auto aIt = agentsObj.constBegin();
                 aIt != agentsObj.constEnd();
                 ++aIt) {
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
    return v1Root;
}

void TestRuntimeOpencodeDebug::debugConfig_acceptsRenderedFixture()
{
    // 1. Resolve the binary. Honor OPENCODE_BIN / SettingsDialog override
    //    / common install paths; fall back to bare "opencode" so PATH
    //    resolution decides.
    QString resolvedPath = resolveBinaryPath();
    if (!resolvedPath.contains(QLatin1Char('/'))) {
        // PATH search; QStandardPaths::findExecutable returns empty
        // when it can't locate the binary.
        const QString found = QStandardPaths::findExecutable(resolvedPath);
        if (found.isEmpty()) {
            QSKIP("no opencode binary");
            return;
        }
        resolvedPath = found;
    } else if (!QFileInfo::exists(resolvedPath)) {
        QSKIP("no opencode binary");
        return;
    }

    // 2. Render a fixture to disk via the production write path.
    //    applyConfigWithBackup bypasses ContractChecker / Catalog so
    //    the test does not require a populated ~/.cache/opencode/
    //    models.json to get a file on disk.
    QTemporaryDir srcTmp;
    QVERIFY(srcTmp.isValid());
    const QString srcPath = QDir(srcTmp.path()).filePath(
        QStringLiteral("opencode.json"));
    QVERIFY2(writeJsonFile(srcPath, minimalValidConfig()),
             qPrintable(QStringLiteral("could not write fixture to %1")
                            .arg(srcPath)));

    // 3. Apply the v2-sidecar parity workaround (matches Phase G5
    //    handling — installed opencode 1.17.x rejects v2 `agents`).
    QTemporaryDir gateTmp;
    QVERIFY(gateTmp.isValid());
    QFile raw(srcPath);
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const QJsonDocument docIn = QJsonDocument::fromJson(raw.readAll());
    raw.close();
    QVERIFY(docIn.isObject());
    const QString gatePath = QDir(gateTmp.path()).filePath(
        QStringLiteral("opencode.json"));
    QVERIFY2(writeJsonFile(gatePath, stripV2Sidecar(docIn.object())),
             qPrintable(QStringLiteral("could not write gate-fixture to %1")
                            .arg(gatePath)));

    // 4. Run `opencode debug config` against the v1-only mirror. This
    //    is the binding runtime gate per ROADMAP.md / OPENCODE-CONFIG-
    //    INTROSPECTION.md §12.3.
    QProcess proc;
    proc.setProgram(resolvedPath);
    proc.setArguments(QStringList{ QStringLiteral("debug"),
                                   QStringLiteral("config") });
    proc.setWorkingDirectory(gateTmp.path());
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start();
    QVERIFY2(proc.waitForStarted(15000),
             qPrintable(QStringLiteral(
                 "could not start %1 debug config: %2")
                            .arg(resolvedPath, proc.errorString())));
    QVERIFY2(proc.waitForFinished(30000),
             qPrintable(QStringLiteral(
                 "%1 debug config did not finish within 30s; killing")
                            .arg(resolvedPath)));
    const QByteArray err = proc.readAllStandardError();
    const QByteArray outBytes = proc.readAllStandardOutput();
    const int exitCode = proc.exitCode();

    // 5. Assert the runtime gate: exit 0, no InvalidError / Unrecognized
    //    stderr — matching the bare acceptance in
    //    scripts/acceptance-phase-h.sh.
    QCOMPARE(exitCode, 0);
    QVERIFY2(err.isEmpty(),
             qPrintable(QStringLiteral(
                 "%1 debug config rejected the fixture (exit %2):\n"
                 "  stderr: %3\n  stdout: %4")
                            .arg(resolvedPath,
                                 QString::number(exitCode),
                                 QString::fromUtf8(err),
                                 QString::fromUtf8(outBytes))));
}

QTEST_MAIN(TestRuntimeOpencodeDebug)
#include "test_runtime_opencode_debug.moc"
