#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "apply_helpers.h"

class TestApplyHelpers : public QObject
{
    Q_OBJECT

private slots:
    void createsFileWithoutBackupWhenNoExisting();
    void createsBackupAndReplacesContentWhenExisting();
};

void TestApplyHelpers::createsFileWithoutBackupWhenNoExisting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString targetPath = dir.filePath(QStringLiteral("opencode.json"));

    QJsonObject config;
    config.insert(QStringLiteral("answer"), 42);

    const ApplyResult result = applyConfigWithBackup(targetPath, config);
    QVERIFY(result.success);
    QVERIFY(result.errorString.isEmpty());
    QVERIFY(result.backupPath.isEmpty());

    QFile file(targetPath);
    QVERIFY(file.exists());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("answer")).toInt(), 42);
}

void TestApplyHelpers::createsBackupAndReplacesContentWhenExisting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString targetPath = dir.filePath(QStringLiteral("opencode.json"));

    // Write an initial config.
    {
        QFile file(targetPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray initialData = QByteArrayLiteral("{\n  \"value\": 1\n}\n");
        QVERIFY(file.write(initialData) == initialData.size());
    }

    QJsonObject newConfig;
    newConfig.insert(QStringLiteral("value"), 2);

    const ApplyResult result = applyConfigWithBackup(targetPath, newConfig);
    QVERIFY(result.success);
    QVERIFY(result.errorString.isEmpty());
    QVERIFY(!result.backupPath.isEmpty());

    // Backup should exist and contain the old value.
    QFile backup(result.backupPath);
    QVERIFY(backup.exists());
    QVERIFY(backup.open(QIODevice::ReadOnly));
    const QJsonDocument backupDoc = QJsonDocument::fromJson(backup.readAll());
    QVERIFY(backupDoc.isObject());
    QCOMPARE(backupDoc.object().value(QStringLiteral("value")).toInt(), 1);

    // Target should now contain the new value.
    QFile file(targetPath);
    QVERIFY(file.exists());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument newDoc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(newDoc.isObject());
    QCOMPARE(newDoc.object().value(QStringLiteral("value")).toInt(), 2);
}

QTEST_MAIN(TestApplyHelpers)
#include "test_apply.moc"
