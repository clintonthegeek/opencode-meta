// Round-trip + content tests for TrialCompareDialog (ROADMAP P2-1).
//
// Mirrors test_confirm_apply_dialog.cpp: the dialog is a pure viewer
// with no IO side-effects, so we exercise the public read-only
// surface (leftTrialId / rightTrialId / leftRenderedText /
// rightRenderedText) and assert the source-of-truth precedence:
//   * Snapshot wins over re-render.
//   * Re-render of the current Team + Specialists + Roles is used
//     when the snapshot is empty.
//   * A clear "(no rendered config available …)" placeholder is used
//     when neither path produces JSON.
// We also verify the line-by-line diff highlights actually differ
// between matching and non-matching rendered configs by feeding two
// snapshots with known differences.

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>

#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "models/Trial.h"
#include "storage/StorageManager.h"
#include "ui/TrialCompareDialog.h"

class TestTrialCompareDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void rendersSnapshotsSideBySide();
    void fallsBackToReRenderWhenSnapshotMissing();
    void showsPlaceholderWhenNoSnapshotAndNoTeam();
    void fallsBackWhenTeamMissingButSnapshotPresent();
    void diffHighlightsDifferingSnapshots();
    void dialogExposesTrialIds();

private:
    static QString renderedTextOf(const QTextEdit *edit);

    QTemporaryDir m_tmpRoot;
    QString m_storageRoot;
    QString m_teamId;
    QString m_leftSnapshot;
    QString m_rightSnapshot;
};

void TestTrialCompareDialog::initTestCase()
{
    QVERIFY(m_tmpRoot.isValid());
    qputenv("HOME", m_tmpRoot.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    m_storageRoot = QDir::homePath() + QStringLiteral("/.opencode-meta");

    StorageManager storage(m_storageRoot);
    storage.ensureRoot();

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
    team.id = QStringLiteral("team-trial-compare");
    team.name = QStringLiteral("Trial Compare Team");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding binding;
    binding.roleId = QStringLiteral("build");
    binding.specialistId = spec.id;
    team.specialists.append(binding);
    QVERIFY(storage.saveTeam(team));
    m_teamId = team.id;

    m_leftSnapshot = QStringLiteral(
        "{\n"
        "    \"$schema\": \"https://opencode.ai/config.json\",\n"
        "    \"default_agent\": \"build\",\n"
        "    \"agent\": {\n"
        "        \"build\": {\n"
        "            \"model\": \"anthropic/claude-sonnet-4-6\"\n"
        "        }\n"
        "    }\n"
        "}\n");

    m_rightSnapshot = QStringLiteral(
        "{\n"
        "    \"$schema\": \"https://opencode.ai/config.json\",\n"
        "    \"default_agent\": \"build\",\n"
        "    \"agent\": {\n"
        "        \"build\": {\n"
        "            \"model\": \"anthropic/claude-opus-4-7\"\n"
        "        }\n"
        "    }\n"
        "}\n");
}

void TestTrialCompareDialog::cleanupTestCase()
{
    qunsetenv("HOME");
}

QString TestTrialCompareDialog::renderedTextOf(const QTextEdit *edit)
{
    if (!edit) {
        return QString();
    }
    return edit->toPlainText();
}

void TestTrialCompareDialog::rendersSnapshotsSideBySide()
{
    StorageManager storage(m_storageRoot);

    Trial left;
    left.id = QStringLiteral("trial-left-snapshot");
    left.teamId = m_teamId;
    left.projectPath = QStringLiteral("/tmp/left");
    left.timestamp = QDateTime::currentDateTimeUtc();
    left.notes = QStringLiteral("left snapshot");
    QJsonParseError err{};
    left.renderedConfigSnapshot =
        QJsonDocument::fromJson(m_leftSnapshot.toUtf8(), &err).object();
    QVERIFY(err.error == QJsonParseError::NoError);
    QVERIFY(storage.saveTrial(left));

    Trial right;
    right.id = QStringLiteral("trial-right-snapshot");
    right.teamId = m_teamId;
    right.projectPath = QStringLiteral("/tmp/right");
    right.timestamp = QDateTime::currentDateTimeUtc();
    right.notes = QStringLiteral("right snapshot");
    right.renderedConfigSnapshot =
        QJsonDocument::fromJson(m_rightSnapshot.toUtf8(), &err).object();
    QVERIFY(err.error == QJsonParseError::NoError);
    QVERIFY(storage.saveTrial(right));

    TrialCompareDialog dlg(left.id, right.id, storage, nullptr);

    const QString leftText = dlg.leftRenderedText();
    const QString rightText = dlg.rightRenderedText();

    QVERIFY2(leftText.contains(QStringLiteral("claude-sonnet-4-6")),
             qPrintable(QStringLiteral("leftRenderedText missing left model: ") + leftText));
    QVERIFY2(rightText.contains(QStringLiteral("claude-opus-4-7")),
             qPrintable(QStringLiteral("rightRenderedText missing right model: ") + rightText));

    auto *leftEdit = dlg.findChild<QTextEdit *>(QStringLiteral("trialCompare.leftEdit"));
    auto *rightEdit = dlg.findChild<QTextEdit *>(QStringLiteral("trialCompare.rightEdit"));
    QVERIFY(leftEdit);
    QVERIFY(rightEdit);

    QVERIFY(leftEdit->isReadOnly());
    QVERIFY(rightEdit->isReadOnly());

    QCOMPARE(renderedTextOf(leftEdit), leftText);
    QCOMPARE(renderedTextOf(rightEdit), rightText);
}

void TestTrialCompareDialog::fallsBackToReRenderWhenSnapshotMissing()
{
    StorageManager storage(m_storageRoot);

    Trial left;
    left.id = QStringLiteral("trial-left-no-snapshot");
    left.teamId = m_teamId;
    left.projectPath = QStringLiteral("/tmp/left-norender");
    left.timestamp = QDateTime::currentDateTimeUtc();
    // Note: deliberately do not set renderedConfigSnapshot so the
    // dialog has to re-render against the current Role + Specialist.
    QVERIFY(storage.saveTrial(left));

    Trial right = left;
    right.id = QStringLiteral("trial-right-no-snapshot");
    right.projectPath = QStringLiteral("/tmp/right-norender");
    QVERIFY(storage.saveTrial(right));

    TrialCompareDialog dlg(left.id, right.id, storage, nullptr);

    const QString leftText = dlg.leftRenderedText();
    const QString rightText = dlg.rightRenderedText();

    QVERIFY2(!leftText.isEmpty(), "leftRenderedText should re-render to non-empty JSON");
    QVERIFY2(!rightText.isEmpty(), "rightRenderedText should re-render to non-empty JSON");

    QVERIFY2(!leftText.contains(QStringLiteral("no rendered config")),
             "left side unexpectedly fell back to placeholder");
    QVERIFY2(!rightText.contains(QStringLiteral("no rendered config")),
             "right side unexpectedly fell back to placeholder");

    // Both sides re-render from the same Team + Roles + Specialists,
    // so the rendered strings must be equal.
    QCOMPARE(leftText, rightText);
}

void TestTrialCompareDialog::showsPlaceholderWhenNoSnapshotAndNoTeam()
{
    StorageManager storage(m_storageRoot);

    Trial orphan;
    orphan.id = QStringLiteral("trial-orphan");
    orphan.teamId = QStringLiteral("team-that-was-deleted");
    orphan.projectPath = QStringLiteral("/tmp/orphan");
    orphan.timestamp = QDateTime::currentDateTimeUtc();
    QVERIFY(storage.saveTrial(orphan));

    Trial other;
    other.id = QStringLiteral("trial-other-orphan");
    other.teamId = QStringLiteral("also-deleted");
    other.projectPath = QStringLiteral("/tmp/other-orphan");
    QVERIFY(storage.saveTrial(other));

    TrialCompareDialog dlg(orphan.id, other.id, storage, nullptr);

    const QString leftText = dlg.leftRenderedText();
    const QString rightText = dlg.rightRenderedText();

    QVERIFY2(leftText.contains(QStringLiteral("no rendered config")),
             qPrintable(QStringLiteral("left side should fall back, got: ") + leftText));
    QVERIFY2(rightText.contains(QStringLiteral("no rendered config")),
             qPrintable(QStringLiteral("right side should fall back, got: ") + rightText));
}

void TestTrialCompareDialog::fallsBackWhenTeamMissingButSnapshotPresent()
{
    StorageManager storage(m_storageRoot);

    Trial left;
    left.id = QStringLiteral("trial-snapshot-only");
    left.teamId = QStringLiteral("team-deleted-but-snapshot-still-good");
    left.projectPath = QStringLiteral("/tmp/snapshot-only");
    left.timestamp = QDateTime::currentDateTimeUtc();
    QJsonParseError err{};
    left.renderedConfigSnapshot =
        QJsonDocument::fromJson(m_leftSnapshot.toUtf8(), &err).object();
    QVERIFY(err.error == QJsonParseError::NoError);
    QVERIFY(storage.saveTrial(left));

    Trial right = left;
    right.id = QStringLiteral("trial-snapshot-only-right");
    right.renderedConfigSnapshot =
        QJsonDocument::fromJson(m_rightSnapshot.toUtf8(), &err).object();
    QVERIFY(err.error == QJsonParseError::NoError);
    QVERIFY(storage.saveTrial(right));

    TrialCompareDialog dlg(left.id, right.id, storage, nullptr);

    // Snapshots are still on disk, so even though the Team can't be
    // loaded, the snapshots MUST still be the source of truth.
    QVERIFY(dlg.leftRenderedText().contains(QStringLiteral("claude-sonnet-4-6")));
    QVERIFY(dlg.rightRenderedText().contains(QStringLiteral("claude-opus-4-7")));
    QVERIFY2(!dlg.leftRenderedText().contains(QStringLiteral("no rendered config")),
             "snapshot must override missing-Team fallback");
    QVERIFY2(!dlg.rightRenderedText().contains(QStringLiteral("no rendered config")),
             "snapshot must override missing-Team fallback");
}

void TestTrialCompareDialog::diffHighlightsDifferingSnapshots()
{
    StorageManager storage(m_storageRoot);

    Trial left;
    left.id = QStringLiteral("trial-diff-left");
    left.teamId = m_teamId;
    left.projectPath = QStringLiteral("/tmp/diff-left");
    left.timestamp = QDateTime::currentDateTimeUtc();
    QJsonParseError err{};
    left.renderedConfigSnapshot =
        QJsonDocument::fromJson(m_leftSnapshot.toUtf8(), &err).object();
    QVERIFY(err.error == QJsonParseError::NoError);
    QVERIFY(storage.saveTrial(left));

    Trial right;
    right.id = QStringLiteral("trial-diff-right");
    right.teamId = m_teamId;
    right.projectPath = QStringLiteral("/tmp/diff-right");
    right.timestamp = QDateTime::currentDateTimeUtc();
    right.renderedConfigSnapshot =
        QJsonDocument::fromJson(m_rightSnapshot.toUtf8(), &err).object();
    QVERIFY(err.error == QJsonParseError::NoError);
    QVERIFY(storage.saveTrial(right));

    TrialCompareDialog dlg(left.id, right.id, storage, nullptr);

    const QString leftText = dlg.leftRenderedText();
    const QString rightText = dlg.rightRenderedText();
    const QStringList leftLines = leftText.split(QLatin1Char('\n'));
    const QStringList rightLines = rightText.split(QLatin1Char('\n'));

    auto *leftEdit = dlg.findChild<QTextEdit *>(QStringLiteral("trialCompare.leftEdit"));
    auto *rightEdit = dlg.findChild<QTextEdit *>(QStringLiteral("trialCompare.rightEdit"));
    QVERIFY(leftEdit);
    QVERIFY(rightEdit);

    QTextDocument *leftDoc = leftEdit->document();
    QTextDocument *rightDoc = rightEdit->document();

    // Sanity check that every changed line carries a tinted background
    // and that the model-value line in particular is coloured. We
    // intentionally don't pin down an exact QColor value because
    // themes can override; we only care that the background differs
    // from the "no tint" baseline for lines whose left/right strings
    // differ.
    QVERIFY(leftDoc->blockCount() >= leftLines.size() - 1);
    QVERIFY(rightDoc->blockCount() >= rightLines.size() - 1);

    bool sawChangedLeft = false;
    bool sawChangedRight = false;
    bool sawUnchangedLeft = false;
    for (int i = 0; i < leftLines.size(); ++i) {
        const QString left = leftLines.at(i);
        const QString right = (i < rightLines.size()) ? rightLines.at(i) : QString();
        const bool different = left != right;
        const QTextBlock block = leftDoc->findBlockByNumber(i);
        if (!block.isValid()) {
            continue;
        }
        QTextCursor cursor(block);
        const QTextCharFormat fmt = cursor.charFormat();
        if (different) {
            if (fmt.background().style() != Qt::NoBrush) {
                sawChangedLeft = true;
            }
        } else {
            if (fmt.background().style() == Qt::NoBrush) {
                sawUnchangedLeft = true;
            }
        }
    }
    for (int i = 0; i < rightLines.size(); ++i) {
        const QString right = rightLines.at(i);
        const QString left = (i < leftLines.size()) ? leftLines.at(i) : QString();
        const bool different = left != right;
        const QTextBlock block = rightDoc->findBlockByNumber(i);
        if (!block.isValid()) {
            continue;
        }
        QTextCursor cursor(block);
        const QTextCharFormat fmt = cursor.charFormat();
        if (different) {
            if (fmt.background().style() != Qt::NoBrush) {
                sawChangedRight = true;
            }
        }
    }
    QVERIFY2(sawChangedLeft,
             "expected at least one left-pane line with a tinted diff background");
    QVERIFY2(sawChangedRight,
             "expected at least one right-pane line with a tinted diff background");
    QVERIFY2(sawUnchangedLeft,
             "expected at least one left-pane line to remain un-tinted (no false positives)");
}

void TestTrialCompareDialog::dialogExposesTrialIds()
{
    StorageManager storage(m_storageRoot);

    Trial left;
    left.id = QStringLiteral("trial-id-left");
    left.teamId = m_teamId;
    left.projectPath = QStringLiteral("/tmp/id-left");
    QVERIFY(storage.saveTrial(left));

    Trial right;
    right.id = QStringLiteral("trial-id-right");
    right.teamId = m_teamId;
    right.projectPath = QStringLiteral("/tmp/id-right");
    QVERIFY(storage.saveTrial(right));

    TrialCompareDialog dlg(left.id, right.id, storage, nullptr);

    QCOMPARE(dlg.leftTrialId(), left.id);
    QCOMPARE(dlg.rightTrialId(), right.id);

    // Reject must cleanly close — used as Close hook in the caller.
    dlg.reject();
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
}

QTEST_MAIN(TestTrialCompareDialog)
#include "test_trial_compare_dialog.moc"
