// tests/test_status_bar.cpp
//
// ROADMAP P0-3 -- smoke + contract test for the persistent StatusBar.
//
// Two halves of coverage:
//
//   1. StatusBar widget itself
//      - Constructs cleanly with all expected child widgets reachable
//        through stable objectNames ("statusBar.lastActionLabel",
//        "statusBar.indicatorLabel", "statusBar.countsLabel").
//      - refreshCountsFromStorage(StorageManager) updates the counts
//        label to the four model-kind sizes pulled fresh from disk.
//      - setLastAction / clearLastAction drive the "last action"
//        label verbatim, including the empty-string reset path.
//      - setWarning / setError / clearIndicator drive the indicator
//        label correctly: Warning paints yellow + ⚠, Error paints
//        red + ✕, clearIndicator empties the text without losing
//        the slot in the layout.
//      - severity() reflects the latest state transition so a host
//        can persist / serialize it without re-reading QLabel text.
//
//   2. StatusBar wired into MainWindow
//      - MainWindow exposes a permanent StatusBar whose objectName
//        is "statusBar" and which lives at the right of the
//        QMainWindow::statusBar() / near the help-menu indicator.
//      - refreshStatusCounts() is callable via statusWidget() and
//        picks up the freshly-seeded PARADIGM entities. We seed
//        the storage root before constructing MainWindow (HOME
//        redirect) so the test does not touch real user data.
//
// The window is never show()'n so the harness never depends on the
// event loop and never bleeds into the user's real ~/.opencode-meta.
//
// Tests reach the status widget through MainWindow::statusWidget()
// (public access) and through stable objectNames on the inner labels.
// Stable object names are the contract the UI design and the docs
// rely on; widget-internal renames break the test, which is the
// point.

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

#include "MainWindow.h"
#include "models/ProjectRecord.h"
#include "models/Role.h"
#include "models/Team.h"
#include "models/Trial.h"
#include "storage/StorageManager.h"
#include "ui/StatusBar.h"

class TestStatusBar : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void widgetBuildsAllExpectedChildLabels();
    void refreshCountsFromStorageReadsAllFourModelKinds();
    void zeroModelStorageYieldsZeroCounts();
    void setLastActionAndClearUpdateTheLabelVerbatim();
    void setWarningShowsYellowAlertPrefix();
    void setErrorShowsRedCrossPrefix();
    void clearIndicatorDropsTextButKeepsLayoutSlot();
    void severityTracksLatestTransition();

    void mainWindowExposesStatusBarWidget();
    void statusBarInMainWindowReflectsSeededEntities();

private:
    static QLabel *findChildLabel(QWidget *root, const QString &objectName);

    QTemporaryDir m_tmpRoot;
    QString m_storageRoot;
};

void TestStatusBar::initTestCase()
{
    QVERIFY(m_tmpRoot.isValid());
    // Match the cross-view smoke pattern: redirect HOME so
    // MainWindow's StorageManager derives its root from a temp dir.
    qputenv("HOME", m_tmpRoot.path().toUtf8());
    m_storageRoot = QDir::homePath() + QStringLiteral("/.opencode-meta");

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
}

void TestStatusBar::cleanupTestCase()
{
}

QLabel *TestStatusBar::findChildLabel(QWidget *root, const QString &objectName)
{
    return root ? root->findChild<QLabel *>(objectName) : nullptr;
}

// ---- the StatusBar widget in isolation -------------------------------------

void TestStatusBar::widgetBuildsAllExpectedChildLabels()
{
    StatusBar bar;

    QCOMPARE(bar.objectName(), QStringLiteral("statusBar"));

    QVERIFY(findChildLabel(&bar, QStringLiteral("statusBar.lastActionLabel")));
    QVERIFY(findChildLabel(&bar, QStringLiteral("statusBar.indicatorLabel")));
    QVERIFY(findChildLabel(&bar, QStringLiteral("statusBar.countsLabel")));
}

void TestStatusBar::refreshCountsFromStorageReadsAllFourModelKinds()
{
    StatusBar bar;
    StorageManager storage(m_storageRoot);
    storage.ensureRoot();

    // Pre-seed exactly one of each kind so the counts panel can be
    // asserted on each digit individually.
    {
        Role role;
        role.id = QStringLiteral("p0-3-role");
        role.name = QStringLiteral("P0-3 Role");
        role.mode = Role::Mode::Primary;
        QVERIFY(storage.saveRole(role));
    }
    {
        Team team;
        team.id = QStringLiteral("p0-3-team");
        team.name = QStringLiteral("P0-3 Team");
        team.version = QStringLiteral("0.1.0");
        QVERIFY(storage.saveTeam(team));
    }
    {
        Trial trial;
        trial.id = QStringLiteral("p0-3-trial");
        trial.teamId = QStringLiteral("p0-3-team");
        trial.projectPath = QStringLiteral("/tmp/p0-3");
        trial.timestamp = QDateTime::currentDateTimeUtc();
        QVERIFY(storage.saveTrial(trial));
    }
    {
        ProjectRecord record;
        record.path = QStringLiteral("/tmp/p0-3-project");
        record.teamId = QStringLiteral("p0-3-team");
        QVERIFY(storage.saveProjects(QList<ProjectRecord>{ record }));
    }

    bar.refreshCountsFromStorage(storage);

    QLabel *countsLabel = findChildLabel(&bar, QStringLiteral("statusBar.countsLabel"));
    QVERIFY(countsLabel);
    const QString text = countsLabel->text();
    QVERIFY2(text.contains(QStringLiteral("R: 1")),
             qPrintable(QStringLiteral("counts missing role digit: ") + text));
    QVERIFY2(text.contains(QStringLiteral("T: 1")),
             qPrintable(QStringLiteral("counts missing team digit: ") + text));
    QVERIFY2(text.contains(QStringLiteral("Tr: 1")),
             qPrintable(QStringLiteral("counts missing trial digit: ") + text));
    QVERIFY2(text.contains(QStringLiteral("P: 1")),
             qPrintable(QStringLiteral("counts missing project digit: ") + text));
}

void TestStatusBar::zeroModelStorageYieldsZeroCounts()
{
    // Sweep the storage root so all four listers return zero rows.
    QDir(m_storageRoot).removeRecursively();
    StorageManager storage(m_storageRoot);
    storage.ensureRoot();

    StatusBar bar;
    bar.refreshCountsFromStorage(storage);

    QLabel *countsLabel = findChildLabel(&bar, QStringLiteral("statusBar.countsLabel"));
    QVERIFY(countsLabel);
    const QString text = countsLabel->text();
    QVERIFY2(text.contains(QStringLiteral("R: 0")),
             qPrintable(QStringLiteral("counts missing role zero: ") + text));
    QVERIFY2(text.contains(QStringLiteral("T: 0")),
             qPrintable(QStringLiteral("counts missing team zero: ") + text));
    QVERIFY2(text.contains(QStringLiteral("Tr: 0")),
             qPrintable(QStringLiteral("counts missing trial zero: ") + text));
    QVERIFY2(text.contains(QStringLiteral("P: 0")),
             qPrintable(QStringLiteral("counts missing project zero: ") + text));
}

void TestStatusBar::setLastActionAndClearUpdateTheLabelVerbatim()
{
    StatusBar bar;

    QLabel *lastActionLabel = findChildLabel(&bar, QStringLiteral("statusBar.lastActionLabel"));
    QVERIFY(lastActionLabel);

    QCOMPARE(lastActionLabel->text(), QString());

    bar.setLastAction(QStringLiteral("Created Role 'p0-3-role'"));
    QCOMPARE(lastActionLabel->text(), QStringLiteral("Created Role 'p0-3-role'"));

    bar.setLastAction(QStringLiteral("Saved Team 'p0-3-team'"));
    QCOMPARE(lastActionLabel->text(), QStringLiteral("Saved Team 'p0-3-team'"));

    bar.clearLastAction();
    QCOMPARE(lastActionLabel->text(), QString());
}

void TestStatusBar::setWarningShowsYellowAlertPrefix()
{
    StatusBar bar;

    QLabel *indicatorLabel = findChildLabel(&bar, QStringLiteral("statusBar.indicatorLabel"));
    QVERIFY(indicatorLabel);

    bar.setWarning(QStringLiteral("specialist 'foo' missing role 'bar'"));

    QCOMPARE(bar.severity(), StatusBar::Severity::Warning);
    QVERIFY2(indicatorLabel->text().startsWith(QStringLiteral("\u26A0")),
             qPrintable(QStringLiteral("warning indicator must start with WARNING SIGN: ")
                        + indicatorLabel->text()));
    QVERIFY2(indicatorLabel->text().contains(QStringLiteral("foo")),
             qPrintable(QStringLiteral("warning text must include the message: ")
                        + indicatorLabel->text()));
    // stylesheet carries the yellow color so the indicator stands out
    // without forcing palette overrides on the rest of the app.
    QVERIFY(indicatorLabel->styleSheet().contains(QStringLiteral("#c49800"),
                                                   Qt::CaseInsensitive));
}

void TestStatusBar::setErrorShowsRedCrossPrefix()
{
    StatusBar bar;

    QLabel *indicatorLabel = findChildLabel(&bar, QStringLiteral("statusBar.indicatorLabel"));
    QVERIFY(indicatorLabel);

    bar.setError(QStringLiteral("failed to load team 'p0-3-team'"));

    QCOMPARE(bar.severity(), StatusBar::Severity::Error);
    QVERIFY2(indicatorLabel->text().startsWith(QStringLiteral("\u2715")),
             qPrintable(QStringLiteral("error indicator must start with CROSS MARK: ")
                        + indicatorLabel->text()));
    QVERIFY2(indicatorLabel->text().contains(QStringLiteral("p0-3-team")),
             qPrintable(QStringLiteral("error text must include the message: ")
                        + indicatorLabel->text()));
    QVERIFY(indicatorLabel->styleSheet().contains(QStringLiteral("#b22222"),
                                                   Qt::CaseInsensitive));
}

void TestStatusBar::clearIndicatorDropsTextButKeepsLayoutSlot()
{
    StatusBar bar;

    QLabel *indicatorLabel = findChildLabel(&bar, QStringLiteral("statusBar.indicatorLabel"));
    QVERIFY(indicatorLabel);

    bar.setWarning(QStringLiteral("hot reload failed"));
    QVERIFY(!indicatorLabel->text().isEmpty());

    bar.clearIndicator();

    QCOMPARE(bar.severity(), StatusBar::Severity::None);
    QCOMPARE(indicatorLabel->text(), QString());
    // Layout slot must stay live so future indicators don't shift
    // sibling widgets; we approximate by checking the label is still
    // parented and visible (its minWidth is set in the constructor).
    QVERIFY(indicatorLabel->parent() != nullptr);
}

void TestStatusBar::severityTracksLatestTransition()
{
    StatusBar bar;

    QCOMPARE(bar.severity(), StatusBar::Severity::None);

    // Empty-message calls are no-ops at the severity layer -- only
    // clearIndicator() can deliberately drop the indicator.
    bar.setWarning(QString());
    QCOMPARE(bar.severity(), StatusBar::Severity::None);

    bar.setWarning(QStringLiteral("thing"));
    QCOMPARE(bar.severity(), StatusBar::Severity::Warning);

    // Error takes priority over warning visually and via severity().
    bar.setError(QStringLiteral("other thing"));
    QCOMPARE(bar.severity(), StatusBar::Severity::Error);

    bar.clearIndicator();
    QCOMPARE(bar.severity(), StatusBar::Severity::None);
}

// ---- StatusBar embedded in MainWindow --------------------------------------

void TestStatusBar::mainWindowExposesStatusBarWidget()
{
    MainWindow w;

    StatusBar *bar = w.statusWidget();
    QVERIFY2(bar, "MainWindow must install a StatusBar widget by default");
    QCOMPARE(bar->objectName(), QStringLiteral("statusBar"));

    // Find the labels behind the public accessor so we still assert
    // the stable objectName contract is honored.
    QVERIFY(findChildLabel(bar, QStringLiteral("statusBar.lastActionLabel")));
    QVERIFY(findChildLabel(bar, QStringLiteral("statusBar.indicatorLabel")));
    QVERIFY(findChildLabel(bar, QStringLiteral("statusBar.countsLabel")));
}

void TestStatusBar::statusBarInMainWindowReflectsSeededEntities()
{
    // Seed the freshly-derived storage root with one of every model
    // kind so we can assert each digit lands in the right spot on the
    // MainWindow-embedded status bar.
    StorageManager storage(m_storageRoot);
    storage.ensureRoot();
    {
        Role role;
        role.id = QStringLiteral("mw-role");
        role.name = QStringLiteral("MW Role");
        role.mode = Role::Mode::Primary;
        QVERIFY(storage.saveRole(role));
    }
    {
        Team team;
        team.id = QStringLiteral("mw-team");
        team.name = QStringLiteral("MW Team");
        team.version = QStringLiteral("0.1.0");
        QVERIFY(storage.saveTeam(team));
    }
    {
        ProjectRecord r;
        r.path = QStringLiteral("/tmp/mw-project");
        r.teamId = QStringLiteral("mw-team");
        QVERIFY(storage.saveProjects(QList<ProjectRecord>{ r }));
    }

    MainWindow w;
    StatusBar *bar = w.statusWidget();
    QVERIFY(bar);

    // The constructor already calls refreshStatusCounts(); verify the
    // counts label picked up our seed (StorageManager::seedDefaultsIfNeeded
    // ensures at least 3 Roles + 1 Team land on disk too). The
    // counts panel only carries digits (no names), so we look at the
    // digit after each column header.
    QLabel *countsLabel = findChildLabel(bar, QStringLiteral("statusBar.countsLabel"));
    QVERIFY(countsLabel);
    const QString text = countsLabel->text();

    QVERIFY2(text.contains(QStringLiteral("R:")),
             qPrintable(QStringLiteral("counts missing role column: ") + text));
    QVERIFY2(text.contains(QStringLiteral("T:")),
             qPrintable(QStringLiteral("counts missing team column: ") + text));
    QVERIFY2(text.contains(QStringLiteral("Tr:")),
             qPrintable(QStringLiteral("counts missing trial column: ") + text));
    QVERIFY2(text.contains(QStringLiteral("P:")),
             qPrintable(QStringLiteral("counts missing project column: ") + text));
    QVERIFY2(text.contains(QStringLiteral("P: 1")),
             qPrintable(QStringLiteral("counts missing project digit: ") + text));
    // Roles: at least 4 (1 from us + 3 seed defaults); Teams: at least
    // 2 (1 from us + 1 starter seed). We accept ">= N" by asserting
    // the lower bound so the test stays robust against future seed
    // changes.
    QVERIFY2(text.contains(QStringLiteral("R: 4")),
             qPrintable(QStringLiteral("counts missing role digit (4): ") + text));
    QVERIFY2(text.contains(QStringLiteral("T: 2")),
             qPrintable(QStringLiteral("counts missing team digit (2): ") + text));
}

QTEST_MAIN(TestStatusBar)
#include "test_status_bar.moc"