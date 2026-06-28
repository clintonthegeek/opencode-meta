#include "apply_helpers.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

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
