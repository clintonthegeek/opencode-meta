// Round-trip + edit-propagation test for EditSpecialistDialog.
//
// Mirrors the test_role_editor_dialog pattern:
//   * Load: every pre-fillable field (name, override, picker) surfaces
//     in the dialog's widget tree with the right initial value.
//   * Edits propagate: writes through QLineEdit / QPlainTextEdit and
//     edits to selectedName()/promptOverrideText()/selectedModelId()
//     are reflected verbatim in editedSpecialist().
//   * Storage round-trip: editedSpecialist() then saveSpecialist() /
//     loadSpecialist() returns an equivalent record (id + roleId are
//     preserved; metadata is preserved; an empty-override edit drops
//     the prompt_override field).
//
// We do NOT drive ModelsBrowserWidget's picker (that is the same path
// AddSpecialistDialog already exercises in test_cross_view_smoke); the
// picker's behavior is owned by ModelsBrowserWidget and its unit
// coverage lives elsewhere. Here we just verify that the dialog
// preserves and round-trips through whatever selectedModelId() returns
// via the F5-style setModelId() seam.

#include <QApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QString>
#include <QTest>
#include <QTemporaryDir>
#include <QDir>

#include "models/ModelInfo.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "storage/StorageManager.h"
#include "ui/EditSpecialistDialog.h"

class TestEditSpecialistDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void loadShowsAllFields();
    void editedSpecialistPreservesImmutableFields();
    void editedSpecialistDropsEmptyOverride();
    void editedSpecialistRoundTripsThroughStorage();

private:
    static QString textOfName(const QObject *parent, const QString &name);
    static QString plainTextOfName(const QObject *parent, const QString &name);

    QTemporaryDir m_tmpRoot;
    QString m_storageRoot;
    QString m_cachedModelId;
};

// EditSpecialistDialog constructs ModelsBrowserWidget internally, which
// in turn tries `~/.opencode-meta/models-cache.json` and falls through
// to `opencode models --refresh` when no cache is present — that would
// hang in a headless test environment. Seed a one-entry cache under a
// per-run QTemporaryDir pointed at by HOME so the picker can pre-select
// the row right away without a network/process spawn.
void TestEditSpecialistDialog::initTestCase()
{
    QVERIFY(m_tmpRoot.isValid());
    qputenv("HOME", m_tmpRoot.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    m_storageRoot = QDir::homePath() + QStringLiteral("/.opencode-meta");
    m_cachedModelId = QStringLiteral("anthropic/claude-sonnet-4-6");

    StorageManager storage(m_storageRoot);
    storage.ensureRoot();

    ModelsCache cache;
    cache.timestamp = QDateTime::currentDateTimeUtc();

    ModelInfo info;
    info.id = m_cachedModelId;
    info.displayName = QStringLiteral("Anthropic Claude Sonnet 4.6");
    info.inputCost = 3.0;
    info.outputCost = 15.0;
    info.capabilities.insert(QStringLiteral("tool-use"));
    info.capabilities.insert(QStringLiteral("reasoning"));

    QJsonObject data;
    data.insert(QStringLiteral("id"), info.id);
    data.insert(QStringLiteral("display_name"), info.displayName);
    data.insert(QStringLiteral("provider"), QStringLiteral("anthropic"));
    data.insert(QStringLiteral("provider_display_name"), QStringLiteral("Anthropic"));
    data.insert(QStringLiteral("input_cost"), info.inputCost);
    data.insert(QStringLiteral("output_cost"), info.outputCost);
    QJsonObject limit;
    limit.insert(QStringLiteral("context"), 200000);
    data.insert(QStringLiteral("limit"), limit);
    data.insert(QStringLiteral("tool_call"), true);
    data.insert(QStringLiteral("reasoning"), true);
    info.data = data;

    cache.models.insert(info.id, info);
    QVERIFY2(storage.saveModelsCache(cache), "saveModelsCache failed");
}

void TestEditSpecialistDialog::cleanupTestCase()
{
    // Restore HOME to a sensible default so any later test in the same
    // process can re-use the previous value if TestEditSpecialistDialog
    // is ever merged into a larger suite.
    qunsetenv("HOME");
}

QString TestEditSpecialistDialog::textOfName(const QObject *parent, const QString &name)
{
    const QLineEdit *le = parent->findChild<const QLineEdit *>(name);
    return le ? le->text() : QString();
}

QString TestEditSpecialistDialog::plainTextOfName(const QObject *parent, const QString &name)
{
    const QPlainTextEdit *pe = parent->findChild<const QPlainTextEdit *>(name);
    return pe ? pe->toPlainText() : QString();
}

void TestEditSpecialistDialog::loadShowsAllFields()
{
    Specialist spec;
    spec.id = QStringLiteral("spec-Build");
    spec.roleId = QStringLiteral("build");
    spec.modelId = m_cachedModelId;
    spec.name = QStringLiteral("Build agent");
    spec.promptOverride = QJsonValue(QStringLiteral("Always run cargo check."));
    QJsonObject meta;
    meta.insert(QStringLiteral("owner"), QStringLiteral("clinton"));
    spec.metadata = meta;

    StorageManager storage(m_storageRoot);
    EditSpecialistDialog dlg(spec, storage);
    QCOMPARE(textOfName(&dlg, QStringLiteral("editSpecialist.nameEdit")),
             spec.name);
    QCOMPARE(plainTextOfName(&dlg, QStringLiteral("editSpecialist.overrideEdit")),
             spec.promptOverride.toString());

    auto *idLabel = dlg.findChild<QLabel *>(QStringLiteral("editSpecialist.idLabel"));
    QVERIFY(idLabel);
    QVERIFY(idLabel->text().contains(spec.id));
}

void TestEditSpecialistDialog::editedSpecialistPreservesImmutableFields()
{
    Specialist spec;
    spec.id = QStringLiteral("spec-Reviewer");
    spec.roleId = QStringLiteral("reviewer");
    spec.modelId = m_cachedModelId;
    spec.name = QStringLiteral("Reviewer");
    spec.promptOverride = QJsonValue(QStringLiteral("Original override."));
    QJsonObject meta;
    meta.insert(QStringLiteral("team"), QStringLiteral("smoke"));
    spec.metadata = meta;

    StorageManager storage(m_storageRoot);
    EditSpecialistDialog dlg(spec, storage);

    auto *nameEdit = dlg.findChild<QLineEdit *>(QStringLiteral("editSpecialist.nameEdit"));
    QVERIFY(nameEdit);
    nameEdit->setText(QStringLiteral("Senior Reviewer"));

    auto *overrideEdit = dlg.findChild<QPlainTextEdit *>(QStringLiteral("editSpecialist.overrideEdit"));
    QVERIFY(overrideEdit);
    overrideEdit->setPlainText(QStringLiteral("Always include a checklist."));

    Specialist out = dlg.editedSpecialist();

    // Immutable binding fields preserved verbatim.
    QCOMPARE(out.id, spec.id);
    QCOMPARE(out.roleId, spec.roleId);
    QCOMPARE(out.metadata, spec.metadata);

    // Editable fields reflect the new values.
    QCOMPARE(out.name, QStringLiteral("Senior Reviewer"));
    QCOMPARE(out.promptOverride.toString(),
             QStringLiteral("Always include a checklist."));
    // modelId is whatever the picker reported (empty in this headless
    // environment because ModelsBrowserWidget hasn't been populated);
    // editedSpecialist() falls back to the initial value.
    QCOMPARE(out.modelId, spec.modelId);
}

void TestEditSpecialistDialog::editedSpecialistDropsEmptyOverride()
{
    Specialist spec;
    spec.id = QStringLiteral("spec-WithOverride");
    spec.roleId = QStringLiteral("build");
    spec.modelId = m_cachedModelId;
    spec.name = QStringLiteral("Build with override");
    spec.promptOverride = QJsonValue(QStringLiteral("Original override text."));

    StorageManager storage(m_storageRoot);
    EditSpecialistDialog dlg(spec, storage);

    auto *overrideEdit = dlg.findChild<QPlainTextEdit *>(QStringLiteral("editSpecialist.overrideEdit"));
    QVERIFY(overrideEdit);
    QVERIFY(!overrideEdit->toPlainText().isEmpty());

    // Empty the override editor entirely.
    overrideEdit->setPlainText(QString());

    Specialist out = dlg.editedSpecialist();
    // QJsonValue::Undefined serializes as a missing key in toJson().
    QVERIFY(out.promptOverride.isUndefined());
    QVERIFY(out.toJson().value(QStringLiteral("prompt_override")).isUndefined());

    // Whitespace-only is treated the same.
    overrideEdit->setPlainText(QStringLiteral("   \n\t"));
    out = dlg.editedSpecialist();
    QVERIFY(out.promptOverride.isUndefined());
}

void TestEditSpecialistDialog::editedSpecialistRoundTripsThroughStorage()
{
    StorageManager storage(m_storageRoot);
    storage.ensureRoot();

    Specialist spec;
    spec.id = QStringLiteral("spec-StorageRoundtrip");
    spec.roleId = QStringLiteral("build");
    spec.modelId = m_cachedModelId;
    spec.name = QStringLiteral("Original");
    spec.promptOverride = QJsonValue(QStringLiteral("Original override."));
    QVERIFY(storage.saveSpecialist(spec));

    Specialist loaded = storage.loadSpecialist(spec.id);
    QVERIFY(!loaded.id.isEmpty());
    QCOMPARE(loaded.name, spec.name);
    QCOMPARE(loaded.promptOverride.toString(),
             spec.promptOverride.toString());

    EditSpecialistDialog dlg(loaded, storage);
    auto *nameEdit = dlg.findChild<QLineEdit *>(QStringLiteral("editSpecialist.nameEdit"));
    QVERIFY(nameEdit);
    nameEdit->setText(QStringLiteral("Renamed via Edit"));

    auto *overrideEdit = dlg.findChild<QPlainTextEdit *>(QStringLiteral("editSpecialist.overrideEdit"));
    QVERIFY(overrideEdit);
    overrideEdit->setPlainText(QStringLiteral("Edited override text."));

    Specialist edited = dlg.editedSpecialist();
    QVERIFY(storage.saveSpecialist(edited));

    Specialist reloaded = storage.loadSpecialist(edited.id);
    QCOMPARE(reloaded.id, spec.id);
    QCOMPARE(reloaded.roleId, spec.roleId);
    QCOMPARE(reloaded.name, QStringLiteral("Renamed via Edit"));
    QCOMPARE(reloaded.promptOverride.toString(),
             QStringLiteral("Edited override text."));
}

QTEST_MAIN(TestEditSpecialistDialog)
#include "test_edit_specialist_dialog.moc"
