#include "apply_helpers.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include "generation/ContractChecker.h"
#include "generation/ProviderCatalog.h"

ApplyResult applyConfigWithBackup(const QString &targetPath,
                                  const QJsonObject &config)
{
    ApplyResult result;

    if (targetPath.isEmpty()) {
        result.errorString = QStringLiteral("Empty target path");
        qDebug() << "applyConfigWithBackup: empty target path";
        return result;
    }

    QFileInfo targetInfo(targetPath);
    QDir dir = targetInfo.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            result.errorString = QStringLiteral("Failed to create directory %1").arg(dir.path());
            qDebug() << "applyConfigWithBackup:" << result.errorString;
            return result;
        }
    }

    QString backupPath;
    const bool hasExistingFile = targetInfo.exists() && targetInfo.isFile();
    if (hasExistingFile) {
        const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmss"));
        backupPath = targetPath + QStringLiteral(".") + timestamp + QStringLiteral(".bak");

        if (!QFile::copy(targetPath, backupPath)) {
            result.errorString = QStringLiteral("Failed to create backup at %1").arg(backupPath);
            qDebug() << "applyConfigWithBackup:" << result.errorString;
            return result;
        }
    }

    const QString tmpPath = targetPath + QStringLiteral(".tmp");
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.errorString = QStringLiteral("Failed to open %1 for writing: %2")
                                 .arg(tmpPath, tmpFile.errorString());
        qDebug() << "applyConfigWithBackup:" << result.errorString;
        return result;
    }

    const QJsonDocument doc(config);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = tmpFile.write(data);
    if (written != data.size()) {
        result.errorString = QStringLiteral("Failed to write complete config to %1").arg(tmpPath);
        qDebug() << "applyConfigWithBackup:" << result.errorString;
        tmpFile.close();
        QFile::remove(tmpPath);
        return result;
    }
    tmpFile.close();

    // Replace the target file with the temporary file contents.
    if (hasExistingFile) {
        if (!QFile::remove(targetPath)) {
            result.errorString = QStringLiteral("Failed to remove existing file %1 before replace").arg(targetPath);
            qDebug() << "applyConfigWithBackup:" << result.errorString;
            QFile::remove(tmpPath);
            return result;
        }
    }

    if (!QFile::rename(tmpPath, targetPath)) {
        result.errorString = QStringLiteral("Failed to move %1 into place as %2")
                                 .arg(tmpPath, targetPath);
        qDebug() << "applyConfigWithBackup:" << result.errorString;

        // Best-effort cleanup; the backup (if any) is left intact so
        // the user can restore manually.
        QFile::remove(tmpPath);
        return result;
    }

    result.success = true;
    result.backupPath = backupPath;
    return result;
}

ApplyResult commit(const QString &targetPath, const QJsonObject &config)
{
    return commit(targetPath, config, nullptr);
}

ApplyResult commit(const QString &targetPath,
                   const QJsonObject &config,
                   const ProviderCatalog *catalog)
{
    // Pre-write contract gate (Phase G3 spec). The check is the
    // ContractChecker::validate() that mirrors opencode's
    // ConfigParse.schema (report §12.3 + parse.ts:74–78): unknown keys
    // trip the topLevelExtraKeys check at load time. We surface the
    // first violation here so the user never sees a runtime
    // InvalidError from a Team / Trial / manual-export apply.
    //
    // D-2 / C0-2: live-catalog enforcement. Production calls
    // (StorageManager::applyTeamToProject, ConfirmApplyDialog, etc.)
    // always pass `&ProviderCatalog::instance()` as `catalog`. When
    // the caller hands us a non-null catalog pointer, the live-catalog
    // check is in force: if the catalog is not yet loaded (e.g. the
    // user has never run `opencode models --refresh`, or the cache
    // file is missing / unparseable), we REFUSE to write and return
    // a clear "provider catalog not loaded" error. The historical
    // silent fallback to the structural-only §8.3 check has been
    // removed per the D-2 rejection rationale ("files that nobody
    // can run are useless to users"). The structural-only path is
    // still available via the `commit(targetPath, config)` overload
    // for callers who knowingly have no catalog (e.g. test fixtures
    // that exercise `apply_helpers::commit` directly).
    if (catalog && !catalog->isLoaded()) {
        ApplyResult result;
        result.success = false;
        result.errorString = QStringLiteral(
            "provider catalog not loaded; refusing to write %1. "
            "Run `opencode models --refresh` to populate the catalog, "
            "or use the structural-only commit(target, config) overload.")
            .arg(targetPath.isEmpty() ? QStringLiteral("<empty path>") : targetPath);
        qWarning() << "apply_helpers::commit:" << result.errorString;
        return result;
    }

    QString contractError;
    const bool ok = catalog
        ? ContractChecker::validate(config, catalog, &contractError)
        : ContractChecker::validate(config, &contractError);
    if (!ok) {
        ApplyResult result;
        result.success = false;
        result.errorString = QStringLiteral(
            "contract check failed for %1 (no file written): %2")
            .arg(targetPath.isEmpty() ? QStringLiteral("<empty path>") : targetPath,
                 contractError);
        qWarning() << "apply_helpers::commit:" << result.errorString;
        return result;
    }
    return applyConfigWithBackup(targetPath, config);
}
