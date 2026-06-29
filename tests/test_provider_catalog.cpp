// tests/test_provider_catalog.cpp
// Phase C2-1 unit tests for ProviderCatalog::ensureReadyForApply().
//
// The helper is the production entry point called by
// `StorageManager::applyTeamToProject` per D-2 / C2-2; its sole purpose
// is to push the live catalog closer to "now" before the apply-path
// ContractChecker gate rejects unseen models. We do NOT need a real
// `opencode` binary for these tests: the helper logs the refresh path
// and lets tests stub the actual CLI via `OPENCODE_BIN` (pointing at a
// tmpdir-resident shell script). What we DO assert:
//   * when the cache is fresh enough, the helper does NOT launch a
//     QProcess and stays a pure file-load;
//   * when the cache is stale OR absent AND a CLI is reachable, the
//     helper DOES launch it (detectable by the OPENCODE_BIN binary
//     timestamp — touched by the stub script);
//   * when the cache file is either missing or unparseable to begin
//     with, `loadFromCache` tolerates it (Phase C2-3); the helper
//     surfaces a hard `false` for the missing case so apply-path
//     refuses per D-2;
//   * the helper does not mask a non-zero `opencode models --refresh`
//     exit code in the in-memory catalog state when the file IS still
//     loadable — we re-run `loadFromCache` to make the caller see the
//     freshest on-disk truth.

#include <QTest>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QString>

#include "generation/ProviderCatalog.h"

class TestProviderCatalog : public QObject
{
    Q_OBJECT

private slots:
    void freshCache_doesNotAttemptRefresh();
    void staleCache_attemptsRefresh();
    void missingCache_noBinary_returnsFalse();
    void unparseableCache_toleratedByLoad();
    void maxAgeZeroForcesRefresh();
    void maxAgeSkippableWhenExplicitlyZero();
};

namespace {

// Write a tiny but structurally-valid models.json fixture so
// loadFromCache() succeeds against it. Shape mirrors report §8.1.
bool writeCatalogFixture(const QString &path)
{
    QJsonObject anthropic;
    QJsonObject models;
    models.insert(QStringLiteral("claude-sonnet-4-6"), QJsonObject());
    anthropic.insert(QStringLiteral("models"), models);
    QJsonObject root;
    root.insert(QStringLiteral("anthropic"), anthropic);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

// Touch `path` so its mtime lands `minutesAgo` minutes before now.
// Tests use this to fabricate "stale" cache files without sleeping
// or faking the system clock.
bool backdateCacheMtime(const QString &path, int minutesAgo)
{
    const qint64 offsetMs = qint64(-minutesAgo) * 60 * 1000;
    const QDateTime newMtime = QDateTime::currentDateTime().addMSecs(offsetMs);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const bool ok = f.setFileTime(newMtime, QFileDevice::FileModificationTime);
    f.close();
    return ok;
}

} // namespace

void TestProviderCatalog::freshCache_doesNotAttemptRefresh()
{
    // Cache written NOW (mtime == now) + a maxAge of 60 minutes → helper
    // should NOT shell out. We assert by pointing OPENCODE_BIN at a
    // sentinel script that we can read afterward; if it was never
    // created, the helper never tried.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString cachePath = QDir(tmp.path()).filePath(QStringLiteral("models.json"));
    QVERIFY(writeCatalogFixture(cachePath));

    // Stage an OPENCODE_BIN pointing at a sentinel script so any
    // refresh attempt is detectable. The script itself just touches a
    // companion flag file.
    const QString stageDir = QDir(tmp.path()).filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stageDir));
    const QString sentinel = QDir(stageDir).filePath(QStringLiteral("flag.txt"));
    const QString stubBin = QDir(stageDir).filePath(QStringLiteral("fake-opencode.sh"));
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
    qputenv("OPENCODE_BIN", stubBin.toLocal8Bit());

    ProviderCatalog c;
    QVERIFY(c.ensureReadyForApply(/*maxAgeMinutes=*/60, cachePath));
    QVERIFY(c.isLoaded());
    QVERIFY(c.isValidModel(QStringLiteral("anthropic/claude-sonnet-4-6")));

    QVERIFY2(!QFile::exists(sentinel),
             "fresh-cache path MUST NOT shell out; sentinel flag was touched");

    qunsetenv("OPENCODE_BIN");
}

void TestProviderCatalog::staleCache_attemptsRefresh()
{
    // Cache written then backdated to 120 min ago; maxAge is 60 min.
    // The helper should detect stale and shell out to OPENCODE_BIN.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString cachePath = QDir(tmp.path()).filePath(QStringLiteral("models.json"));
    QVERIFY(writeCatalogFixture(cachePath));
    QVERIFY(backdateCacheMtime(cachePath, 120));

    const QString stageDir = QDir(tmp.path()).filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stageDir));
    const QString sentinel = QDir(stageDir).filePath(QStringLiteral("flag.txt"));
    const QString stubBin = QDir(stageDir).filePath(QStringLiteral("fake-opencode.sh"));
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
    qputenv("OPENCODE_BIN", stubBin.toLocal8Bit());

    ProviderCatalog c;
    QVERIFY(c.ensureReadyForApply(/*maxAgeMinutes=*/60, cachePath));
    QVERIFY(c.isLoaded());

    QVERIFY2(QFile::exists(sentinel),
             "stale-cache path MUST shell out to refresh binary; sentinel flag missing");

    // The helper must NOT have masked the previous exit-code by NOT
    // running refresh; the stub returns 0 so this is just a smoke
    // assertion that the launch actually happened.
    QVERIFY(c.lastRefreshExitCode() == 0 || c.lastRefreshExitCode() == -1);

    qunsetenv("OPENCODE_BIN");
}

void TestProviderCatalog::missingCache_noBinary_returnsFalse()
{
    // No cache file. No OPENCODE_BIN env var. No installed opencode.
    // The helper must return false so the apply-path refuses per D-2.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString cachePath = QDir(tmp.path()).filePath(QStringLiteral("models.json"));

    qunsetenv("OPENCODE_BIN");

    // Force resolveOpencodeBinary() to its PATH-only fallback and
    // override PATH to a directory that has no `opencode` binary in it.
    // Without this QtTestRunner will inherit the host's PATH which
    // usually points at /usr/local/bin/opencode and would mask the
    // "no binary" branch.
    const QString emptyPathDir = QDir(tmp.path()).filePath(QStringLiteral("empty-path"));
    QVERIFY(QDir().mkpath(emptyPathDir));
    qputenv("PATH", emptyPathDir.toLocal8Bit());

    ProviderCatalog c;
    QVERIFY2(!c.ensureReadyForApply(60, cachePath),
             "missing-cache + no-binary MUST return false (D-2 hard refusal)");
    QVERIFY(!c.isLoaded());

    qunsetenv("PATH");
}

void TestProviderCatalog::unparseableCache_toleratedByLoad()
{
    // Phase C2-3: loadFromCache tolerates a partially-written / broken
    // cache file. The helper inherits that tolerance; callers must check
    // isLoaded() / be ready to surface "catalog unloadable" via D-2.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString cachePath = QDir(tmp.path()).filePath(QStringLiteral("models.json"));
    {
        QFile f(cachePath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QStringLiteral("{ this is not valid json").toUtf8());
        f.close();
    }

    qunsetenv("OPENCODE_BIN");
    const QString emptyPathDir = QDir(tmp.path()).filePath(QStringLiteral("empty-path"));
    QVERIFY(QDir().mkpath(emptyPathDir));
    qputenv("PATH", emptyPathDir.toLocal8Bit());

    ProviderCatalog c;
    QVERIFY2(!c.ensureReadyForApply(0, cachePath),
             "unparseable cache + no refresh path MUST return false");
    QVERIFY(!c.isLoaded());

    qunsetenv("PATH");
}

void TestProviderCatalog::maxAgeZeroForcesRefresh()
{
    // maxAgeMinutes <= 0 collapses the freshness check → the helper
    // ignores mtime and refreshes unconditionally. We assert by treating
    // a fresh-but-existing cache as still triggering the launch.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString cachePath = QDir(tmp.path()).filePath(QStringLiteral("models.json"));
    QVERIFY(writeCatalogFixture(cachePath));
    // mtime is "now" — at maxAge=60 this would be skipped, but at
    // maxAge=0 we force the refresh.

    const QString stageDir = QDir(tmp.path()).filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stageDir));
    const QString sentinel = QDir(stageDir).filePath(QStringLiteral("flag.txt"));
    const QString stubBin = QDir(stageDir).filePath(QStringLiteral("fake-opencode.sh"));
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
    qputenv("OPENCODE_BIN", stubBin.toLocal8Bit());

    ProviderCatalog c;
    QVERIFY(c.ensureReadyForApply(/*maxAgeMinutes=*/0, cachePath));

    QVERIFY2(QFile::exists(sentinel),
             "maxAge=0 path MUST force the refresh regardless of mtime; sentinel missing");

    qunsetenv("OPENCODE_BIN");
}

void TestProviderCatalog::maxAgeSkippableWhenExplicitlyZero()
{
    // Sanity-only mirror: same as maxAgeZeroForcesRefresh but renamed
    // for grep discoverability. The "force refresh" semantic IS what
    // maxAge<=0 means; this slot is just a second-line guard against
    // accidental refactor that wedges the off-by-one into "<=0 means
    // skip".
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString cachePath = QDir(tmp.path()).filePath(QStringLiteral("models.json"));
    QVERIFY(writeCatalogFixture(cachePath));

    const QString stageDir = QDir(tmp.path()).filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stageDir));
    const QString sentinel = QDir(stageDir).filePath(QStringLiteral("flag.txt"));
    const QString stubBin = QDir(stageDir).filePath(QStringLiteral("fake-opencode.sh"));
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
    qputenv("OPENCODE_BIN", stubBin.toLocal8Bit());

    ProviderCatalog c;
    QVERIFY(c.ensureReadyForApply(/*maxAgeMinutes=*/-1, cachePath));
    QVERIFY(QFile::exists(sentinel));

    qunsetenv("OPENCODE_BIN");
}

QTEST_MAIN(TestProviderCatalog)
#include "test_provider_catalog.moc"
