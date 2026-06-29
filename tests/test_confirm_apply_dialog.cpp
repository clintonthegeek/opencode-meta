// Round-trip + content tests for ConfirmApplyDialog (ROADMAP P1-5).
//
// Mirrors the test_edit_specialist_dialog pattern: the dialog itself
// does not write to disk, so we exercise the public read-only surface
// (projectPath / teamId / teamName / renderedText / summaryText) and
// verify both the side-by-side diff highlights differ and the banner
// text adapts to file presence + JSON validity. The "real" apply path
// is owned by ProjectsWidget::switchTeamForProject (or any future
// host), with StorageManager::applyTeamToProject() — already covered
// by test_apply_team. Here we just lock down the gate's contract:
//   * no existing file -> "create" banner;
//   * existing valid JSON -> "overwrite + backup" banner;
//   * existing invalid JSON -> orange "invalid" banner;
//   * renderedText is non-empty JSON (renderer succeeded);
//   * accept()/reject() round-trip cleanly with the host.

#include <QApplication>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QDir>

#include "models/Role.h"
#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/ConfirmApplyDialog.h"

class TestConfirmApplyDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void bannerSummarizesCreateWhenMissing();
    void bannerSummarizesOverwriteWhenValidJson();
    void bannerSummarizesInvalidWhenUnparseable();
    void renderedTextIsValidJson();
    void dialogExposesProjectPathAndTeam();
    void acceptFinishesWithoutIo();

private:
    static QString summaryTextOf(const QObject *parent);

    QTemporaryDir m_tmpRoot;
    QString m_storageRoot;
    QString m_teamId;
};

void TestConfirmApplyDialog::initTestCase()
{
    QVERIFY(m_tmpRoot.isValid());
    qputenv("HOME", m_tmpRoot.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    m_storageRoot = QDir::homePath() + QStringLiteral("/.opencode-meta");

    StorageManager storage(m_storageRoot);
    storage.ensureRoot();

    // Seed the Role + Specialist + Team the dialog will render against.
    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    role.systemPrompt = QJsonValue(QStringLiteral("You are the primary build agent."));
    role.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(role));

    Specialist spec;
    spec.id = QStringLiteral("spec-build");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.name = QStringLiteral("Build agent");
    QVERIFY(storage.saveSpecialist(spec));

    Team team;
    team.id = QStringLiteral("team-confirm-dialog");
    team.name = QStringLiteral("Confirm Dialog Team");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding binding;
    binding.roleId = QStringLiteral("build");
    binding.specialistId = spec.id;
    team.specialists.append(binding);
    QVERIFY(storage.saveTeam(team));

    m_teamId = team.id;
}

void TestConfirmApplyDialog::cleanupTestCase()
{
    qunsetenv("HOME");
}

QString TestConfirmApplyDialog::summaryTextOf(const QObject *parent)
{
    auto *label = parent->findChild<QLabel *>(QStringLiteral("confirmApply.summary"));
    return label ? label->text() : QString();
}

void TestConfirmApplyDialog::bannerSummarizesCreateWhenMissing()
{
    StorageManager storage(m_storageRoot);
    Team team = storage.loadTeam(m_teamId);
    QVERIFY(!team.id.isEmpty());

    // No existing file content -> banner must say "create".
    ConfirmApplyDialog dlg(QStringLiteral("/tmp/fake-project"),
                           team, storage,
                           QString(),     // empty -> no existing file
                           false,         // currentIsJson ignored here
                           nullptr);
    const QString summary = summaryTextOf(&dlg);
    QVERIFY2(summary.contains(QStringLiteral("new")),
             qPrintable(QStringLiteral("expected 'new' banner, got: ") + summary));
    QVERIFY(!summary.contains(QStringLiteral("already exists"), Qt::CaseInsensitive));
    QVERIFY(!summary.contains(QStringLiteral("not valid"), Qt::CaseInsensitive));
}

void TestConfirmApplyDialog::bannerSummarizesOverwriteWhenValidJson()
{
    StorageManager storage(m_storageRoot);
    Team team = storage.loadTeam(m_teamId);

    const QString existing = QStringLiteral(
        "{\n"
        "    \"$schema\": \"https://opencode.ai/config.json\"\n"
        "}\n");

    ConfirmApplyDialog dlg(QStringLiteral("/tmp/fake-project"),
                           team, storage,
                           existing,
                           true,          // valid JSON
                           nullptr);
    const QString summary = summaryTextOf(&dlg);
    QVERIFY2(summary.contains(QStringLiteral("already exists"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("expected 'already exists' banner, got: ") + summary));
    QVERIFY2(summary.contains(QStringLiteral("backup"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("expected backup notice, got: ") + summary));
}

void TestConfirmApplyDialog::bannerSummarizesInvalidWhenUnparseable()
{
    StorageManager storage(m_storageRoot);
    Team team = storage.loadTeam(m_teamId);

    ConfirmApplyDialog dlg(QStringLiteral("/tmp/fake-project"),
                           team, storage,
                           QStringLiteral("{not valid json"),
                           false,         // not valid JSON
                           nullptr);
    const QString summary = summaryTextOf(&dlg);
    QVERIFY2(summary.contains(QStringLiteral("not valid"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("expected 'not valid' banner, got: ") + summary));
}

void TestConfirmApplyDialog::renderedTextIsValidJson()
{
    StorageManager storage(m_storageRoot);
    Team team = storage.loadTeam(m_teamId);

    ConfirmApplyDialog dlg(QStringLiteral("/tmp/fake-project"),
                           team, storage,
                           QString(),
                           false,
                           nullptr);

    const QString rendered = dlg.renderedText();
    QVERIFY2(!rendered.isEmpty(), "renderedText should not be empty when the renderer succeeds");

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(rendered.toUtf8(), &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError,
             qPrintable(QStringLiteral("renderedText is not valid JSON: ") + parseError.errorString()));
    QVERIFY(doc.isObject());
}

void TestConfirmApplyDialog::dialogExposesProjectPathAndTeam()
{
    StorageManager storage(m_storageRoot);
    Team team = storage.loadTeam(m_teamId);

    const QString projectPath = QStringLiteral("/home/clinton/dev/example");
    ConfirmApplyDialog dlg(projectPath, team, storage, QString(), false, nullptr);

    QCOMPARE(dlg.projectPath(), projectPath);
    QCOMPARE(dlg.teamId(), team.id);
    QCOMPARE(dlg.teamName(), team.name);
    QVERIFY(dlg.renderedText().contains(QStringLiteral("$schema")));
}

void TestConfirmApplyDialog::acceptFinishesWithoutIo()
{
    // The dialog MUST NOT touch disk on accept. Spot-check by giving
    // it an obviously bad project path and confirming accept() still
    // returns Accepted cleanly (the gate has no IO side effects — the
    // caller is what writes the file).
    StorageManager storage(m_storageRoot);
    Team team = storage.loadTeam(m_teamId);

    ConfirmApplyDialog dlg(QStringLiteral("/this/path/does/not/exist"),
                           team, storage, QString(), false, nullptr);

    // Reject dial path: confirm QDialog::reject() works without IO.
    dlg.reject();
    QCOMPARE(dlg.result(), int(QDialog::Rejected));

    // Now test accept cleanly. We avoid exec() to keep tests headless,
    // but the standard pattern still works.
    QSignalSpy acceptSpy(&dlg, &QDialog::accepted);
    QVERIFY(acceptSpy.isValid());
    auto *buttonBox = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(buttonBox);
    QDialogButtonBox::StandardButton okButton = QDialogButtonBox::Ok;
    buttonBox->button(okButton)->click();
    QCOMPARE(dlg.result(), int(QDialog::Accepted));
    QCOMPARE(acceptSpy.count(), 1);
}

QTEST_MAIN(TestConfirmApplyDialog)
#include "test_confirm_apply_dialog.moc"
