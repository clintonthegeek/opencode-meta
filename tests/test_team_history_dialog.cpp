// Tests for TeamHistoryDialog (ROADMAP P3-3).
//
// Mirrors the test_bundle_dialog / test_settings_dialog pattern: the
// dialog is a self-contained QDialog that reads from a populated
// StorageManager but only mutates storage when the user invokes
// "Revert to Selected..." (via StorageManager::revertTeamToVersion,
// which the storage-layer test already locks down). Here we exercise:
//   * Stable object names for every visible widget.
//   * Empty-history empty state + revert button disable.
//   * Sorted (newest-first) row population.
//   * Selection-driven details-pane rendering.
//   * Revert flow without the modal-confirmation prompt (the public
//     applyRevert(int) seam is the test target; the slot wraps it
//     with QMessageBox::question()).
//   * Refreshing the dialog after external snapshot changes.
//
// Each test seeds its own storage state so failures are localized and
// test order does not influence counts. A shared initTestCase seed
// would muddle the count assertions whenever a previous test mutated
// the storage (e.g. applyRevert adds a new head snapshot).

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

#include "models/Team.h"
#include "models/TeamVersion.h"
#include "storage/StorageManager.h"
#include "ui/TeamHistoryDialog.h"

class TestTeamHistoryDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructorHasStableObjectNames();
    void emptyHistoryShowsEmptyHintAndDisablesRevert();
    void populatedHistorySortsNewestFirst();
    void selectingRowUpdatesDetailsPane();
    void applyRevertRestoresSnapshotAndEmitsSignal();
    void refreshReloadsAfterExternalHistoryChange();

private:
    // Each test seeds its own Team + history. The team id is provided
    // by the caller so every test uses a unique storage path — that
    // preserves cross-test isolation: a previous test's revert or
    // refresh call never shows up in the next test's count assertions.
    static void seedThreeSteps(StorageManager &storage,
                                const QString &teamId);

    QTemporaryDir m_tmpRoot;
    QString m_storageRoot;
};

// Helper: write a Team and bump it twice so we end up with 2 snapshots.
// The two snapshots carry team.version="1.0.0" (snap-of-v1) and
// team.version="1.1.0" (snap-of-v2) respectively.
void TestTeamHistoryDialog::seedThreeSteps(StorageManager &storage,
                                           const QString &teamId)
{
    Team v1;
    v1.id = teamId;
    v1.name = QStringLiteral("History Dialog Team");
    v1.description = QStringLiteral("dialog test fixture");
    v1.version = QStringLiteral("1.0.0");
    v1.primarySpecialistIds.append(QStringLiteral("spec-a"));
    {
        Team::SpecialistBinding b;
        b.roleId = QStringLiteral("build");
        b.specialistId = QStringLiteral("spec-a");
        v1.specialists.append(b);
    }
    QVERIFY(storage.saveTeamWithSnapshot(v1,
                                         QString::fromLatin1(TeamVersion::kReasonAutoCreate),
                                         QStringLiteral("seed")));

    Team v2 = v1;
    v2.version = QStringLiteral("1.1.0");
    v2.description = QStringLiteral("after rename");
    QVERIFY(storage.saveTeamWithSnapshot(v2,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit),
                                         QStringLiteral("rename")));

    Team v3 = v2;
    v3.version = QStringLiteral("1.2.0");
    v3.description = QStringLiteral("after specialize swap");
    QVERIFY(storage.saveTeamWithSnapshot(v3,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit),
                                         QStringLiteral("swap specialist")));
}

void TestTeamHistoryDialog::initTestCase()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QVERIFY(m_tmpRoot.isValid());
    QVERIFY(QDir(m_tmpRoot.path()).mkpath(QStringLiteral(".")));
    m_storageRoot = QDir::cleanPath(m_tmpRoot.path());
}

void TestTeamHistoryDialog::constructorHasStableObjectNames()
{
    StorageManager storage(m_storageRoot);
    TeamHistoryDialog dlg(QStringLiteral("history-dialog-team"), storage);

    QCOMPARE(dlg.objectName(), QStringLiteral("teamHistoryDialog"));
    QCOMPARE(dlg.teamId(), QStringLiteral("history-dialog-team"));

    QVERIFY(dlg.headerLabel());
    QVERIFY(dlg.snapshotTable());
    QVERIFY(dlg.detailsEdit());
    QVERIFY(dlg.refreshButton());
    QVERIFY(dlg.revertButton());

    // Verify the objectName-based findChild hooks work as documented.
    QVERIFY(dlg.findChild<QLabel *>(QStringLiteral("teamHistory.headerLabel")));
    QVERIFY(dlg.findChild<QTableWidget *>(QStringLiteral("teamHistory.snapshotsTable")));
    QVERIFY(dlg.findChild<QPlainTextEdit *>(QStringLiteral("teamHistory.detailsEdit")));
    QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("teamHistory.refreshButton")));
    QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("teamHistory.revertButton")));

    // details pane starts non-interactable.
    QVERIFY(dlg.detailsEdit()->isReadOnly());
}

void TestTeamHistoryDialog::emptyHistoryShowsEmptyHintAndDisablesRevert()
{
    StorageManager storage(m_storageRoot);
    TeamHistoryDialog dlg(QStringLiteral("totally-no-history-team"), storage);

    QCOMPARE(dlg.snapshotTable()->rowCount(), 0);
    QVERIFY(!dlg.revertButton()->isEnabled());

    // The empty-state label flips visible once the table is empty.
    QVERIFY(dlg.findChild<QLabel *>(QStringLiteral("teamHistory.emptyLabel")));
}

void TestTeamHistoryDialog::populatedHistorySortsNewestFirst()
{
    StorageManager storage(m_storageRoot);
    seedThreeSteps(storage, QStringLiteral("t-populated"));
    TeamHistoryDialog dlg(QStringLiteral("t-populated"), storage);

    auto *table = dlg.snapshotTable();
    QCOMPARE(table->rowCount(), 2);

    QTableWidgetItem *row0Reason = table->item(0, 2);
    QVERIFY(row0Reason);
    QCOMPARE(row0Reason->text(), QString::fromLatin1(TeamVersion::kReasonAutoEdit));

    QTableWidgetItem *row0Spec = table->item(0, 3);
    QCOMPARE(row0Spec->text(), QStringLiteral("1"));

    QTableWidgetItem *row0Ts = table->item(0, 0);
    QVERIFY(!row0Ts->text().isEmpty());

    QVERIFY(dlg.revertButton()->isEnabled());
}

void TestTeamHistoryDialog::selectingRowUpdatesDetailsPane()
{
    StorageManager storage(m_storageRoot);
    seedThreeSteps(storage, QStringLiteral("t-select"));
    TeamHistoryDialog dlg(QStringLiteral("t-select"), storage);

    // Two snapshots: snap-of-v1 (team.version=1.0.0) older, then
    // snap-of-v2 (team.version=1.1.0) newer. Newest-first sort means
    // row 0 = 1.1.0, row 1 = 1.0.0.
    auto *table = dlg.snapshotTable();
    QCOMPARE(table->rowCount(), 2);

    table->setCurrentCell(1, 0); // v1-snapshot ("1.0.0")
    QApplication::processEvents();

    const QString details = dlg.detailsEdit()->toPlainText();
    QVERIFY(details.contains(QStringLiteral("Snapshot id:")));
    QVERIFY(details.contains(QStringLiteral("Timestamp:")));
    QVERIFY(details.contains(QStringLiteral("Team JSON")));
    QVERIFY(details.contains(QStringLiteral("\"version\": \"1.0.0\"")));
    QVERIFY2(!details.contains(QStringLiteral("\"version\": \"1.1.0\"")),
             qPrintable(QStringLiteral("stale details: ") + details));

    // Flip to row 0 (newest = snap-of-v2 captured at v3 save time).
    table->setCurrentCell(0, 0);
    QApplication::processEvents();
    const QString details2 = dlg.detailsEdit()->toPlainText();
    QVERIFY(details2.contains(QStringLiteral("\"version\": \"1.1.0\"")));
    QVERIFY2(!details2.contains(QStringLiteral("\"version\": \"1.0.0\"")),
             qPrintable(QStringLiteral("stale details: ") + details2));
}

void TestTeamHistoryDialog::applyRevertRestoresSnapshotAndEmitsSignal()
{
    StorageManager storage(m_storageRoot);
    const QString teamId = QStringLiteral("t-revert");
    seedThreeSteps(storage, teamId);

    QString seenTeamId;
    QString seenSnapshotId;
    int signalCount = 0;

    TeamHistoryDialog dlg(teamId, storage);
    QVERIFY(dlg.findChild<QLabel *>(QStringLiteral("teamHistory.headerLabel")));

    QSignalSpy spy(&dlg, &TeamHistoryDialog::teamReverted);
    QVERIFY(spy.isValid());
    QObject::connect(&dlg, &TeamHistoryDialog::teamReverted,
                     [&](const QString &id, const QString &snap) {
                         seenTeamId = id;
                         seenSnapshotId = snap;
                         ++signalCount;
                     });

    auto *table = dlg.snapshotTable();
    QCOMPARE(table->rowCount(), 2);

    table->setCurrentCell(1, 0); // older snapshot (snap-of-v1)
    QApplication::processEvents();

    const QString snapId = table->item(1, 0)->data(Qt::UserRole).toString();
    QVERIFY(!snapId.isEmpty());

    // applyRevert is the test seam; onRevertClicked() wraps it with
    // the modal QMessageBox::question().
    QVERIFY(dlg.applyRevert(1));

    QCOMPARE(signalCount, 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(seenTeamId, teamId);
    QCOMPARE(seenSnapshotId, snapId);

    // After revert the live Team should be at the older snapshot
    // (team.version=1.0.0).
    const Team live = storage.loadTeam(teamId);
    QCOMPARE(live.version, QStringLiteral("1.0.0"));
    QCOMPARE(dlg.lastRevertedFromSnapshotId(), snapId);

    // History is one longer (the head snapshot we just produced) — 3 total.
    QCOMPARE(storage.listTeamVersions(teamId).size(), 3);
}

void TestTeamHistoryDialog::refreshReloadsAfterExternalHistoryChange()
{
    StorageManager storage(m_storageRoot);
    const QString teamId = QStringLiteral("t-refresh");
    seedThreeSteps(storage, teamId);
    TeamHistoryDialog dlg(teamId, storage);
    const int initialCount = dlg.snapshotTable()->rowCount();
    QCOMPARE(initialCount, 2);

    // Save one more snapshot from outside the dialog — exercise the
    // public refreshSnapshots() host seam.
    Team edited = storage.loadTeam(teamId);
    edited.version = QStringLiteral("1.3.0");
    QVERIFY(storage.saveTeamWithSnapshot(edited,
                                         QString::fromLatin1(TeamVersion::kReasonAutoEdit),
                                         QStringLiteral("external edit")));

    QCOMPARE(dlg.snapshotTable()->rowCount(), initialCount); // unchanged pre-refresh
    dlg.refreshSnapshots();
    QCOMPARE(dlg.snapshotTable()->rowCount(), initialCount + 1);
}

QTEST_MAIN(TestTeamHistoryDialog)
#include "test_team_history_dialog.moc"
